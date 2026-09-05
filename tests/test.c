/* Host unit tests for the hardware independent modules. */
#include <stdio.h>
#include <string.h>

#include "kbd.h"
#include "telnet.h"
#include "vt.h"

static int total, failed;

static void check(int ok, const char *name) {
  total++;
  if (!ok)
    failed++;
  printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

/* --- vt helpers --- */

static void vt_start(void) {
  vt_reset();
  vt_bell = 0;
  memset(vt_dirty, 0, sizeof(vt_dirty));
}

static int chr_is(uint8_t r, uint8_t c, const void *e, uint8_t n) {
  return !memcmp(vt_chr[r] + c, e, n);
}

static int blanks(uint8_t r, uint8_t c, uint8_t n) {
  while (n--)
    if (vt_chr[r][c + n] != 0x20)
      return 0;
  return 1;
}

static uint8_t ndirty(void) {
  uint8_t i, n = 0;

  for (i = 0; i < VT_ROWS; i++)
    n += vt_dirty[i] != 0;
  return n;
}

static void t_glyphs(void) {
  uint8_t i;

  vt_start();
  vt_puts("Aa 0");
  check(chr_is(0, 0, "\x41\x01\x20\x30", 4), "printable screen codes");
  check(vt_x == 4 && vt_y == 0, "cursor advances");
  check(vt_att[0][0] == VT_DEFFG && !vt_colored, "default colour");
  check(vt_screencode('Z') == 0x5a, "vt_screencode");

  vt_start();
  for (i = 0; i < 40; i++)
    vt_puts("X");
  check(vt_x == 39 && vt_y == 0 && vt_chr[0][39] == 'X',
        "autowrap pends at the last column");
  vt_puts("Y");
  check(vt_y == 1 && vt_x == 1 && vt_chr[1][0] == 'Y', "autowrap to row 1");
}

static void t_c0(void) {
  vt_start();
  vt_puts("AB\rC");
  check(chr_is(0, 0, "CB", 2) && vt_x == 1, "CR to column 0");

  vt_start();
  vt_puts("AB\nC");
  check(vt_y == 1 && vt_chr[1][2] == 'C', "LF keeps the column");

  vt_start();
  vt_puts("AB\bC");
  check(chr_is(0, 0, "AC", 2) && vt_x == 2, "BS backs up");

  vt_start();
  vt_puts("A\tB");
  check(vt_chr[0][8] == 'B' && vt_x == 9, "TAB to the next stop");

  vt_start();
  vt_puts("TOP\033[2;1HNEXT\033[25;1H\n");
  check(chr_is(0, 0, "NEXT", 4) && vt_y == 24, "LF at the bottom scrolls");
}

static void t_cursor(void) {
  vt_start();
  vt_puts("\033[10;20H");
  check(vt_y == 9 && vt_x == 19, "CUP");
  vt_puts("\033[A\033[2B\033[3C\033[D");
  check(vt_y == 10 && vt_x == 21, "CUU CUD CUF CUB");
  vt_puts("\033[15G");
  check(vt_x == 14, "CHA");
  vt_puts("\033[7d");
  check(vt_y == 6, "VPA");
  vt_puts("\033[99;99H");
  check(vt_x == 39 && vt_y == 24, "CUP clamps");
  vt_puts("\033[99A\033[99D");
  check(vt_x == 0 && vt_y == 0, "motion clamps at the edges");
  vt_puts("\033[99B\033[99C");
  check(vt_x == 39 && vt_y == 24, "motion clamps at the far edges");
}

static void t_erase(void) {
  vt_start();
  vt_puts("ABCDEFGH\033[1;4H\033[K");
  check(chr_is(0, 0, "ABC", 3) && blanks(0, 3, 37), "EL to end of line");

  vt_start();
  vt_puts("ABCDEFGH\033[1;4H\033[1K");
  check(blanks(0, 0, 4) && chr_is(0, 4, "EFGH", 4), "EL to start of line");

  vt_start();
  vt_puts("ABCDEFGH\033[1K\033[2K");
  check(blanks(0, 0, 40), "EL whole line");

  vt_start();
  vt_puts("AB\033[2;1HCD\033[1;2H\033[J");
  check(chr_is(0, 0, "A", 1) && blanks(0, 1, 39) && blanks(1, 0, 40),
        "ED to end of screen");

  vt_start();
  vt_puts("AB\033[2;1HCDE\033[2;2H\033[1J");
  check(blanks(0, 0, 40) && blanks(1, 0, 2) && vt_chr[1][2] == 'E',
        "ED to start of screen");

  vt_start();
  vt_puts("AB\033[2;1HCD\033[2J");
  check(blanks(0, 0, 40) && blanks(1, 0, 40), "ED whole screen");
}

static void t_edit(void) {
  vt_start();
  vt_puts("0\r\n1\r\n2\r\n3\r\n4\033[3;1H\033[L");
  check(blanks(2, 0, 40) && vt_chr[3][0] == '2' && vt_chr[5][0] == '4', "IL");
  vt_puts("\033[M");
  check(vt_chr[2][0] == '2' && vt_chr[4][0] == '4', "DL");

  vt_start();
  vt_puts("ABCDEF\033[1;3H\033[2@");
  check(chr_is(0, 0, "AB  CDEF", 8), "ICH");
  vt_puts("\033[2P");
  check(chr_is(0, 0, "ABCDEF", 6) && blanks(0, 6, 34), "DCH");

  vt_start();
  vt_puts("ABCDEF\033[1;3H\033[3X");
  check(chr_is(0, 0, "AB   F", 6), "ECH");
}

static void t_region(void) {
  vt_start();
  vt_puts("T\033[5;10r");
  check(vt_x == 0 && vt_y == 0, "DECSTBM homes the cursor");
  vt_puts("\033[5;1HA\033[6;1HB\033[10;1HZ\n");
  check(vt_chr[4][0] == 'B' && blanks(9, 0, 40) && vt_y == 9,
        "LF scrolls inside the region only");
  check(vt_chr[0][0] == 'T', "rows above the region are untouched");

  vt_start();
  vt_puts("\033[5;10r\033[?6h");
  check(vt_y == 4 && vt_x == 0, "DECOM homes to the region top");
  vt_puts("\033[3;5H");
  check(vt_y == 6 && vt_x == 4, "CUP is region relative under DECOM");
  vt_puts("\033[99;1H");
  check(vt_y == 9, "CUP clamps to the region bottom");
}

static void t_sgr(void) {
  vt_start();
  vt_puts("\033[31mA");
  check(vt_att[0][0] == 2 && vt_chr[0][0] == 'A' && vt_colored,
        "SGR 31 is C64 red");
  vt_puts("\033[0mB");
  check(vt_att[0][1] == VT_DEFFG && vt_chr[0][1] == 'B', "SGR 0 resets");
  vt_puts("\033[7mC");
  check(vt_chr[0][2] == (0x43 | 0x80), "SGR 7 sets the reverse bit");
  vt_puts("\033[0m\033[1;34mD");
  check(vt_att[0][3] == 14 && vt_chr[0][3] == 'D', "SGR 1;34 bright blue");
  vt_puts("\033[0m\033[41mE");
  check(vt_att[0][4] == 2 && vt_chr[0][4] == (0x45 | 0x80),
        "SGR 41 reverses with the background colour");
}

static void t_charset(void) {
  vt_start();
  vt_puts("\033(0qxl\033(BA");
  check(chr_is(0, 0, "\x40\x5d\x70\x41", 4),
        "DEC graphics then ASCII via ESC (");

  vt_start();
  vt_puts("\033)0\016q\017q");
  check(vt_chr[0][0] == 0x40 && vt_chr[0][1] == 0x11, "SO and SI switch G1");
}

static void t_reply(void) {
  vt_start();
  vt_puts("\033[c");
  check(vt_replylen == 5 && !memcmp(vt_reply, "\033[?6c", 5), "DA");

  vt_start();
  vt_puts("\033[10;20H\033[6n");
  check(vt_replylen == 8 && !memcmp(vt_reply, "\033[10;20R", 8), "DSR CPR");
}

static void t_misc(void) {
  vt_start();
  vt_puts("\033[10;20H\0337\033[1;1H\0338");
  check(vt_y == 9 && vt_x == 19, "DECSC and DECRC");

  vt_start();
  vt_puts("\033#8");
  check(chr_is(0, 0, "EEEE", 4) && chr_is(24, 36, "EEEE", 4), "DECALN");

  vt_start();
  vt_puts("\033]0;title\007X");
  check(vt_chr[0][0] == 'X' && vt_x == 1 && !vt_bell && blanks(0, 1, 39),
        "OSC leaves no glyphs");
}

/* --- scroll hooks --- */

static uint8_t hook_ret, hook_calls, out_calls, out_row, out_first;

static uint8_t on_scroll(uint8_t t, uint8_t b, int8_t n) {
  (void)t;
  (void)b;
  (void)n;
  hook_calls++;
  return hook_ret;
}

static void on_out(uint8_t row) {
  out_calls++;
  out_row = row;
  out_first = vt_chr[row][0];
}

static void t_hooks(void) {
  vt_onscroll = on_scroll;
  vt_onscrollout = on_out;
  hook_ret = 1;
  hook_calls = out_calls = 0;

  vt_start();
  vt_puts("A\033[2;1HB\033[25;1H");
  memset(vt_dirty, 0, sizeof(vt_dirty));
  vt_puts("\n");
  check(hook_calls == 1 && ndirty() == 1 && vt_dirty[24],
        "a renderer scroll marks only the blanked row");
  check(out_calls == 1 && out_row == 0 && out_first == 'A',
        "vt_onscrollout sees the row leaving the top");

  hook_ret = 0;
  vt_start();
  vt_puts("\033[25;1H");
  memset(vt_dirty, 0, sizeof(vt_dirty));
  vt_puts("\n");
  check(ndirty() == VT_ROWS, "an unhandled scroll marks every row");

  vt_onscroll = 0;
  vt_onscrollout = 0;
  vt_start();
  vt_puts("\033[25;1H");
  memset(vt_dirty, 0, sizeof(vt_dirty));
  vt_puts("\n");
  check(ndirty() == VT_ROWS, "no hook marks every row");
}

/* --- telnet --- */

static uint8_t cap[64], tbuf[64];
static uint16_t caplen;

static void tn_cap(const uint8_t *p, uint16_t n) {
  while (n-- && caplen < sizeof(cap))
    cap[caplen++] = *p++;
}

static void tn_start(uint8_t mode) {
  tn_init(tn_cap, mode, 40);
  caplen = 0;
}

static uint16_t tn_feed(const void *b, uint16_t n) {
  memcpy(tbuf, b, n);
  return tn_filter(tbuf, n);
}

static int cap_is(const void *e, uint16_t n) {
  return caplen == n && !memcmp(cap, e, n);
}

static void t_telnet(void) {
  static const char do_ttype[] = {'\377', '\375', 24};
  static const char sb_ttype[] = {'\377', '\372', 24, 1, '\377', '\360'};
  static const char iaciac[] = {'a', '\377', '\377', 'b'};
  static const char half1[] = {'x', '\377', '\375'};
  static const char half2[] = {24, 'y'};
  uint8_t o[8];

  tn_start(2);
  check(tn_feed("hi", 2) == 2 && !tn_active, "auto mode waits for an IAC");
  check(tn_feed(do_ttype, 3) == 0 && tn_active && cap_is("\377\373\030", 3),
        "DO TTYPE answered WILL TTYPE");

  caplen = 0;
  check(tn_feed(sb_ttype, 6) == 0 &&
            cap_is("\377\372\030\000VT102\377\360", 11),
        "TTYPE subnegotiation reports VT102");

  caplen = 0;
  check(tn_feed("\377\375\037", 3) == 0 &&
            cap_is("\377\373\037\377\372\037\000\050\000\031\377\360", 12),
        "DO NAWS answered WILL NAWS plus the window size");

  caplen = 0;
  check(tn_feed("\377\375\001", 3) == 0 && cap_is("\377\374\001", 3),
        "DO ECHO refused with WONT");

  caplen = 0;
  check(tn_feed("\377\373\001", 3) == 0 && cap_is("\377\375\001", 3),
        "WILL ECHO answered DO ECHO");

  caplen = 0;
  check(tn_feed("\377\373\143", 3) == 0 && cap_is("\377\376\143", 3),
        "unknown WILL refused with DONT");

  caplen = 0;
  check(tn_feed("\377\375\143", 3) == 0 && cap_is("\377\374\143", 3),
        "unknown DO refused with WONT");

  caplen = 0;
  check(tn_feed(do_ttype, 3) == 0 && !caplen,
        "a repeated request is not answered again");

  tn_start(1);
  check(tn_feed(iaciac, 4) == 3 && !memcmp(tbuf, "a\377b", 3),
        "tn_filter strips commands and unescapes IAC IAC");

  tn_start(1);
  check(tn_feed(half1, 3) == 1 && tbuf[0] == 'x' && !caplen,
        "a split command holds over");
  check(tn_feed(half2, 2) == 1 && tbuf[0] == 'y' && cap_is("\377\373\030", 3),
        "a split command completes");

  tn_start(0);
  check(tn_encode((const uint8_t *)"a", 1, o, sizeof(o)) == 0,
        "tn_encode is a no-op when inactive");
  tn_start(1);
  check(tn_encode((const uint8_t *)"\377", 1, o, sizeof(o)) == 2 &&
            !memcmp(o, "\377\377", 2),
        "tn_encode doubles IAC");
  check(tn_encode((const uint8_t *)"A\r", 2, o, sizeof(o)) == 3 &&
            !memcmp(o, "A\r\000", 3),
        "tn_encode completes a bare CR");
  check(tn_encode((const uint8_t *)"\r\n", 2, o, sizeof(o)) == 2 &&
            !memcmp(o, "\r\n", 2),
        "tn_encode leaves CR LF alone");
}

/* --- keyboard --- */

static int key_is(uint8_t key, const void *e, uint8_t n) {
  uint8_t o[8];

  return kbd_map(key, o) == n && !memcmp(o, e, n);
}

static void t_kbd(void) {
  vt_ckm = vt_lnm = 0;
  check(key_is(0x41, "a", 1) && key_is(0x5a, "z", 1),
        "unshifted letters are lower case");
  check(key_is(0xc1, "A", 1) && key_is(0xda, "Z", 1),
        "shifted letters are upper case");
  check(key_is('7', "7", 1) && key_is('.', ".", 1) && key_is(0x03, "\003", 1),
        "digits punctuation and controls pass through");

  check(key_is(KBD_RETURN, "\r", 1), "RETURN sends CR");
  vt_lnm = 1;
  check(key_is(KBD_RETURN, "\r\n", 2), "RETURN sends CR LF under LNM");
  vt_lnm = 0;

  check(key_is(KBD_UP, "\033[A", 3) && key_is(KBD_DOWN, "\033[B", 3) &&
            key_is(KBD_RIGHT, "\033[C", 3) && key_is(KBD_LEFT, "\033[D", 3),
        "cursor keys");
  vt_ckm = 1;
  check(key_is(KBD_UP, "\033OA", 3) && key_is(KBD_DOWN, "\033OB", 3) &&
            key_is(KBD_RIGHT, "\033OC", 3) && key_is(KBD_LEFT, "\033OD", 3),
        "cursor keys under DECCKM");
  vt_ckm = 0;

  check(key_is(0x85, "\033OP", 3) && key_is(0x89, "\033OQ", 3) &&
            key_is(0x86, "\033OR", 3) && key_is(0x8a, "\033OS", 3),
        "F1 to F4");
  check(key_is(0x14, "\177", 1), "DEL sends 0x7f");
  check(key_is(0x5f, "\033", 1), "left arrow sends ESC");
  check(key_is(0x5c, "\\", 1), "pound sends backslash");
}

int main(void) {
  vt_init(VT_VIEW);
  t_glyphs();
  t_c0();
  t_cursor();
  t_erase();
  t_edit();
  t_region();
  t_sgr();
  t_charset();
  t_reply();
  t_misc();
  t_hooks();
  t_telnet();
  t_kbd();
  printf("%d tests, %d failed\n", total, failed);
  return failed != 0;
}
