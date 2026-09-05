/* See uii.h. Transport: write the command bytes to $df1d, PUSH_CMD, read the
 * response and status queues while the protocol sits in a data state, then
 * DATA_ACC. A "data more" state means another block follows the accept. */
#include <string.h>

#include "uii.h"

#define CONTROL (*(volatile uint8_t *)0xdf1c) /* write */
#define STATUS (*(volatile uint8_t *)0xdf1c)  /* read */
#define CMDDATA (*(volatile uint8_t *)0xdf1d) /* write */
#define IDENT (*(volatile uint8_t *)0xdf1d)   /* read */
#define RESPDATA (*(volatile uint8_t *)0xdf1e)
#define STATDATA (*(volatile uint8_t *)0xdf1f)

#define CTRL_PUSH_CMD 0x01
#define CTRL_DATA_ACC 0x02
#define CTRL_ABORT 0x04
#define CTRL_CLR_ERR 0x08

#define ST_ERROR 0x08
#define ST_STATE 0x30 /* 00 idle, 01 busy, 10 data last, 11 data more */
#define ST_DATA 0x20  /* set for both data states */
#define ST_MORE 0x30
#define ST_STAT_AV 0x40
#define ST_DATA_AV 0x80

#define TARGET_NETWORK 0x03

#define NET_GET_IPADDRESS 0x05
#define NET_TCP_CONNECT 0x07
#define NET_SOCKET_CLOSE 0x09
#define NET_SOCKET_READ 0x10
#define NET_SOCKET_WRITE 0x11

/* Response queue size; a socket read prefixes its payload with a length. */
#define RESP_MAX 896
#define READ_MAX (RESP_MAX - 4)

char uii_status[64];
static uint8_t resp[RESP_MAX];
static uint16_t resplen;

/* ~250ms of spinning per attempt, retried while the interface is busy. */
#define SPIN 20000u
#define TRIES 12

static uint8_t wait_for(uint8_t mask, uint8_t want) {
  uint8_t tries = TRIES;
  do {
    uint16_t spin = SPIN;
    do {
      if ((STATUS & mask) == want)
        return 1;
    } while (--spin);
  } while (--tries);
  CONTROL = CTRL_ABORT;
  return 0;
}

static uint8_t cmd_start(void) { return wait_for(ST_STATE, 0); }

static void cmd_str(const char *s) {
  while (*s)
    CMDDATA = *s++;
}

static void cmd_bytes(const uint8_t *p, uint16_t n) {
  while (n--)
    CMDDATA = *p++;
}

/* Pushes the staged command and collects the reply. Returns 0 on failure. */
static uint8_t cmd_run(void) {
  uint16_t slen = 0;

  resplen = 0;
  uii_status[0] = 0;
  CONTROL = CTRL_PUSH_CMD;
  if (STATUS & ST_ERROR) {
    CONTROL = CTRL_CLR_ERR;
    return 0;
  }

  for (;;) {
    uint8_t more;
    if (!wait_for(ST_DATA, ST_DATA))
      return 0;
    while (STATUS & ST_DATA_AV) {
      uint8_t b = RESPDATA;
      if (resplen < RESP_MAX)
        resp[resplen++] = b;
    }
    while (STATUS & ST_STAT_AV) {
      char b = STATDATA;
      if (slen < sizeof(uii_status) - 1)
        uii_status[slen++] = b;
    }
    more = (STATUS & ST_STATE) == ST_MORE;
    CONTROL = CTRL_DATA_ACC;
    if (!more)
      break;
    /* The state still reads as a data state until the accept is retired. */
    if (!wait_for(ST_DATA, 0))
      return 0;
  }
  uii_status[slen] = 0;
  return 1;
}

uint8_t uii_ok(void) { return uii_status[0] == '0' && uii_status[1] == '0'; }

uint8_t uii_present(void) { return IDENT == UII_ID; }

const uint8_t *uii_ipconfig(void) {
  if (!cmd_start())
    return 0;
  CMDDATA = TARGET_NETWORK;
  CMDDATA = NET_GET_IPADDRESS;
  CMDDATA = 0; /* interface 0 */
  if (!cmd_run() || !uii_ok() || resplen < 16)
    return 0;
  return resp;
}

int16_t uii_connect(const char *host, uint16_t port) {
  if (!cmd_start())
    return -1;
  CMDDATA = TARGET_NETWORK;
  CMDDATA = NET_TCP_CONNECT;
  CMDDATA = port & 0xff;
  CMDDATA = port >> 8;
  cmd_str(host);
  CMDDATA = 0;
  if (!cmd_run() || !uii_ok() || !resplen)
    return -1;
  return resp[0];
}

void uii_close(uint8_t sock) {
  if (!cmd_start())
    return;
  CMDDATA = TARGET_NETWORK;
  CMDDATA = NET_SOCKET_CLOSE;
  CMDDATA = sock;
  cmd_run();
}

int16_t uii_read(uint8_t sock, uint8_t **data, uint16_t want) {
  uint16_t len;

  *data = resp + 2;
  if (want > READ_MAX)
    want = READ_MAX;
  if (!cmd_start())
    return -1;
  CMDDATA = TARGET_NETWORK;
  CMDDATA = NET_SOCKET_READ;
  CMDDATA = sock;
  CMDDATA = want & 0xff;
  CMDDATA = want >> 8;
  if (!cmd_run() || !uii_ok())
    return -1;
  if (resplen < 2)
    return -1;
  /* The firmware reports 0xffff when the socket is merely empty; a length of
   * zero means the peer closed it. */
  len = resp[0] | ((uint16_t)resp[1] << 8);
  if (len == 0xffff)
    return 0;
  if (!len)
    return -1;
  if (len > resplen - 2)
    len = resplen - 2;
  return len;
}

int8_t uii_write(uint8_t sock, const uint8_t *data, uint16_t len) {
  if (!cmd_start())
    return -1;
  CMDDATA = TARGET_NETWORK;
  CMDDATA = NET_SOCKET_WRITE;
  CMDDATA = sock;
  cmd_bytes(data, len);
  if (!cmd_run() || !uii_ok())
    return -1;
  return 0;
}
