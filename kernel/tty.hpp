#pragma once

#include "types.hpp"

void serial_init();
void serial_putc(char c);
char serial_getc();
void serial_diag();
extern volatile int g_poll_rx_count;
void tty_init();
void tty_putc(char c);
void tty_write(const char* str);
void tty_write_dec(uint64_t num);
void tty_write_hex(uint64_t num);
void tty_set_color(uint8_t fg, uint8_t bg);
void tty_scroll();
