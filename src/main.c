/* ulytiterm: a VT102/ANSI terminal for the Commodore 64 that talks to the
 * network through an Ultimate II+ or Ultimate 64 cartridge. */
#include <cbm.h>
#include <string.h>

#include "hist.h"
#include "kbd.h"
#include "reu.h"
#include "screen.h"
#include "telnet.h"
#include "uii.h"
#include "vt.h"

#ifndef VERSION
#define VERSION "dev"
#endif

#define READ_CHUNK 892
#define HOST_MAX 48

static int16_t sock = -1;
static char host[HOST_MAX] = "bbs.fozztexx.com";
static char portstr[6] = "23";
static uint8_t txbuf[64];

static void raw_send(const uint8_t *p, uint16_t n) {
  if (sock >= 0)
    uii_write(sock, p, n);
}

static void send_user(const uint8_t *p, uint16_t n) {
  static uint8_t enc[2 * sizeof(txbuf)];
  uint16_t k = tn_encode(p, n, enc, sizeof(enc));

  raw_send(k ? enc : p, k ? k : n);
}

static void ui_num(uint32_t n) {
  static char b[11];
  uint8_t i = sizeof(b) - 1;

  b[i] = 0;
  do {
    b[--i] = '0' + n % 10;
    n /= 10;
  } while (n);
  vt_puts(b + i);
}

static uint8_t ui_key(void) {
  uint8_t k;

  do {
    scr_flush();
    k = cbm_k_getin();
  } while (!k);
  return k;
}

static void ui_edit(char *buf, uint8_t max) {
  uint8_t n = strlen(buf);
  uint8_t seq[8];

  vt_puts(buf);
  for (;;) {
    uint8_t k = ui_key();
    if (k == KBD_RETURN)
      break;
    if (k == 0x14) {
      if (n) {
        n--;
        vt_puts("\b \b");
      }
      continue;
    }
    if (kbd_map(k, seq) == 1 && seq[0] >= 0x20 && seq[0] < 0x7f &&
        n + 1 < max) {
      buf[n++] = seq[0];
      vt_write(seq, 1);
    }
  }
  buf[n] = 0;
  vt_puts("\r\n");
}

static uint16_t parse_port(void) {
  uint16_t p = 0;
  const char *s = portstr;

  while (*s >= '0' && *s <= '9')
    p = p * 10 + (*s++ - '0');
  return p ? p : 23;
}

/* Returns 0 when the user asks to quit. */
static uint8_t connect_screen(void) {
  const uint8_t *ip;

  vt_puts("\033[2J\033[H\033[7m ulytiterm " VERSION " \033[m\r\n\r\n");
  if (!uii_present()) {
    vt_puts("\033[31mno ultimate command interface.\033[m\r\n"
            "enable it in the cartridge settings.\r\n\r\n");
  } else if ((ip = uii_ipconfig())) {
    uint8_t i;
    vt_puts("address ");
    for (i = 0; i < 4; i++) {
      ui_num(ip[i]);
      vt_puts(i < 3 ? "." : "\r\n");
    }
  }
  vt_puts("reu     ");
  if (reu_banks) {
    ui_num((uint32_t)reu_banks << 6);
    vt_puts("k, ");
    ui_num(hist_max);
    vt_puts(" line scrollback\r\n\r\n");
  } else {
    vt_puts("none\r\n\r\n");
  }
  vt_puts("f5 scrollback  f7 disconnect  f8 80 col\r\n"
          "\033[7mleft arrow\033[m is esc, \033[7mpound\033[m is "
          "backslash\r\n\r\n");
  vt_puts("empty host quits.\r\n\r\nhost: ");
  ui_edit(host, sizeof(host));
  if (!host[0])
    return 0;
  vt_puts("port: ");
  ui_edit(portstr, sizeof(portstr));
  return 1;
}

static void session(void) {
  uint8_t quit = 0;

  tn_init(raw_send, 2, vt_cols);
  while (!quit) {
    uint8_t *p;
    uint8_t txn = 0;
    int16_t n = uii_read(sock, &p, READ_CHUNK);

    if (n < 0)
      break;
    if (n) {
      n = tn_filter(p, n);
      if (n)
        vt_write(p, n);
    }
    scr_flush();
    while (txn + 8 <= sizeof(txbuf)) {
      uint8_t k = cbm_k_getin();
      if (!k)
        break;
      if (k == KBD_F7) {
        quit = 1;
        break;
      }
      if (k == KBD_F8) {
        vt_setcols(vt_cols == VT_VIEW ? VT_MAXCOLS : VT_VIEW);
        tn_resize(vt_cols);
        scr_repaint();
        continue;
      }
      if (k == KBD_F5) {
        hist_view();
        continue;
      }
      txn += kbd_map(k, txbuf + txn);
    }
    if (txn)
      send_user(txbuf, txn);
    if (vt_replylen) {
      send_user(vt_reply, vt_replylen);
      vt_replylen = 0;
    }
  }
}

int main(void) {
  uint8_t romfont;

  reu_init();
  romfont = scr_init();
  vt_init(VT_VIEW, romfont);
  hist_init();

  while (connect_screen()) {
    vt_puts("\r\nconnecting...\r\n");
    scr_flush();
    sock = uii_connect(host, parse_port());
    if (sock < 0) {
      vt_puts("\033[31mfailed: \033[m");
      vt_puts(uii_status);
      vt_puts("\r\npress return\r\n");
      ui_key();
      continue;
    }
    vt_reset();
    session();
    uii_close(sock);
    sock = -1;
    vt_puts("\r\n\033[7mconnection closed\033[m, press return\r\n");
    ui_key();
  }
  scr_done();
  return 0;
}
