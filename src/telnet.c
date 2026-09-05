/* See telnet.h. Options we care about are tracked so that a repeated request
 * is answered only when it changes our state, which avoids negotiation
 * loops; everything else is refused. */
#include "telnet.h"

#define SE 240
#define AYT 246
#define SB 250
#define WILL 251
#define WONT 252
#define DO 253
#define DONT 254
#define IAC 255

#define OPT_BINARY 0
#define OPT_ECHO 1
#define OPT_SGA 3
#define OPT_TTYPE 24
#define OPT_NAWS 31

#define SUB_SEND 1
#define SUB_IS 0

enum { D_DATA, D_IAC, D_NEG, D_SB, D_SB_IAC };

uint8_t tn_active;
uint8_t tn_binary_tx;

static void (*tx)(const uint8_t *, uint16_t);
static uint8_t automode, state, verb, screencols;
#define ST_US_ON 1
#define ST_US_ANS 2
#define ST_HIM_ON 4
#define ST_HIM_ANS 8

static uint8_t opt_state[5];

static uint8_t state_us(int8_t i) { return (opt_state[i] & ST_US_ON) != 0; }

static uint8_t state_him(int8_t i) { return (opt_state[i] & ST_HIM_ON) != 0; }

static uint8_t answered(int8_t i, uint8_t bit) {
  return (opt_state[i] & bit) != 0;
}

static void set_state(int8_t i, uint8_t bits, uint8_t mask) {
  opt_state[i] = (opt_state[i] & ~mask) | bits;
}

static const uint8_t tracked[5] = {OPT_BINARY, OPT_ECHO, OPT_SGA, OPT_TTYPE,
                                   OPT_NAWS};

/* What we will turn on for our side, and what we let the peer turn on. We
 * never echo for the peer, but we do want the peer to echo for us. */
static const uint8_t agree_us[5] = {1, 0, 1, 1, 1};
static const uint8_t agree_him[5] = {1, 1, 1, 0, 0};

static int8_t slot(uint8_t opt) {
  int8_t i;

  for (i = 0; i < 5; i++)
    if (tracked[i] == opt)
      return i;
  return -1;
}

static void send3(uint8_t v, uint8_t opt) {
  uint8_t b[3];

  b[0] = IAC;
  b[1] = v;
  b[2] = opt;
  tx(b, 3);
}

static void send_naws(void) {
  static uint8_t b[9] = {IAC, SB, OPT_NAWS, 0, 0, 0, 25, IAC, SE};

  b[4] = screencols;
  tx(b, sizeof(b));
}

static void send_ttype(void) {
  static const uint8_t b[] = {IAC, SB,  OPT_TTYPE, SUB_IS, 'V', 'T',
                              '1', '0', '2',       IAC,    SE};

  tx(b, sizeof(b));
}

static void negotiate(uint8_t v, uint8_t opt) {
  int8_t i = slot(opt);
  uint8_t on, bit;

  if (i < 0) {
    if (v == WILL)
      send3(DONT, opt);
    else if (v == DO)
      send3(WONT, opt);
    return;
  }
  if (v == DO || v == DONT) {
    on = v == DO && agree_us[i];
    /* Our side of the option: answer the first request, then only changes. */
    bit = (state_us(i) == on) && answered(i, ST_US_ANS);
    if (bit)
      return;
    set_state(i, ST_US_ANS | (on ? ST_US_ON : 0), ST_US_ANS | ST_US_ON);
    send3(on ? WILL : WONT, opt);
    if (on && opt == OPT_NAWS)
      send_naws();
    if (opt == OPT_BINARY)
      tn_binary_tx = on;
  } else {
    on = v == WILL && agree_him[i];
    bit = (state_him(i) == on) && answered(i, ST_HIM_ANS);
    if (bit)
      return;
    set_state(i, ST_HIM_ANS | (on ? ST_HIM_ON : 0), ST_HIM_ANS | ST_HIM_ON);
    send3(on ? DO : DONT, opt);
  }
}

static void subneg(const uint8_t *b, uint8_t n) {
  if (!n)
    return;
  if (b[0] == OPT_TTYPE && n > 1 && b[1] == SUB_SEND)
    send_ttype();
  else if (b[0] == OPT_NAWS)
    send_naws();
}

void tn_init(void (*send)(const uint8_t *, uint16_t), uint8_t mode,
             uint8_t cols) {
  uint8_t i;

  tx = send;
  screencols = cols;
  automode = mode == 2;
  tn_active = mode == 1;
  tn_binary_tx = 0;
  state = D_DATA;
  for (i = 0; i < sizeof(opt_state); i++)
    opt_state[i] = 0;
}

void tn_resize(uint8_t cols) {
  screencols = cols;
  if (opt_state[4] & ST_US_ON) /* NAWS */
    send_naws();
}

uint16_t tn_filter(uint8_t *buf, uint16_t n) {
  static uint8_t sb[16];
  static uint8_t sbn;
  uint16_t i, out = 0;

  if (!tn_active) {
    if (!automode || !n || buf[0] != IAC)
      return n;
    tn_active = 1;
  }
  for (i = 0; i < n; i++) {
    uint8_t c = buf[i];
    switch (state) {
    case D_DATA:
      if (c == IAC)
        state = D_IAC;
      else
        buf[out++] = c;
      break;
    case D_IAC:
      state = D_DATA;
      if (c == IAC)
        buf[out++] = IAC;
      else if (c >= WILL && c <= DONT) {
        verb = c;
        state = D_NEG;
      } else if (c == SB) {
        sbn = 0;
        state = D_SB;
      } else if (c == AYT)
        tx((const uint8_t *)"\r\n[ulytiterm]\r\n", 15);
      break;
    case D_NEG:
      negotiate(verb, c);
      state = D_DATA;
      break;
    case D_SB:
      if (c == IAC)
        state = D_SB_IAC;
      else if (sbn < sizeof(sb))
        sb[sbn++] = c;
      break;
    case D_SB_IAC:
      if (c == IAC) {
        if (sbn < sizeof(sb))
          sb[sbn++] = IAC;
        state = D_SB;
      } else {
        if (c == SE)
          subneg(sb, sbn);
        state = D_DATA;
      }
      break;
    }
  }
  return out;
}

uint16_t tn_encode(const uint8_t *in, uint16_t n, uint8_t *out, uint16_t max) {
  uint16_t i, k = 0;

  if (!tn_active)
    return 0; /* caller sends the original buffer */
  for (i = 0; i < n && k + 2 <= max; i++) {
    uint8_t c = in[i];
    out[k++] = c;
    if (c == IAC)
      out[k++] = IAC;
    else if (c == '\r' && !tn_binary_tx && (i + 1 == n || in[i + 1] != '\n'))
      out[k++] = 0; /* a bare CR must be followed by NUL */
  }
  return k;
}
