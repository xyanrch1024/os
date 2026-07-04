#include "tty.hpp"
#include "port.hpp"

static const int VGA_WIDTH  = 80;
static const int VGA_HEIGHT = 25;
static uint16_t* const VGA_BUFFER = reinterpret_cast<uint16_t*>(0xB8000);

static size_t tty_row;
static size_t tty_col;
static uint8_t tty_color;
static uint16_t* tty_buffer;

#define COM1 0x3F8
#define SER_BUF_SIZE 128

static char ser_buffer[SER_BUF_SIZE];
static int  ser_head, ser_tail;

void serial_init() {
    port_byte_out(COM1 + 1, 0x00);
    port_byte_out(COM1 + 3, 0x80);
    port_byte_out(COM1 + 0, 0x03);
    port_byte_out(COM1 + 1, 0x00);
    port_byte_out(COM1 + 3, 0x03);
    port_byte_out(COM1 + 2, 0xC7);
    port_byte_out(COM1 + 4, 0x0B);

    port_byte_out(COM1 + 1, 0x01);

    ser_head = ser_tail = 0;

    while (port_byte_in(COM1 + 5) & 0x01) port_byte_in(COM1);
}

static int is_transmit_empty() {
    return port_byte_in(COM1 + 5) & 0x20;
}

void serial_putc(char c) {
    if (c == '\n') {
        while (!is_transmit_empty()) {}
        port_byte_out(COM1, '\r');
    }
    while (!is_transmit_empty()) {}
    port_byte_out(COM1, c);
}

extern "C" void serial_rx_irq_handler() {
    while (port_byte_in(COM1 + 5) & 0x01) {
        uint8_t c = port_byte_in(COM1);
        int next = (ser_head + 1) % SER_BUF_SIZE;
        if (next != ser_tail) {
            ser_buffer[ser_head] = static_cast<char>(c);
            ser_head = next;
        }
    }
}

char serial_getc() {
    if (ser_head == ser_tail) return 0;
    char c = ser_buffer[ser_tail];
    ser_tail = (ser_tail + 1) % SER_BUF_SIZE;
    return c;
}

void tty_init() {
    serial_init();
    tty_row    = 0;
    tty_col    = 0;
    tty_color  = 0x0F;
    tty_buffer = VGA_BUFFER;

    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            tty_buffer[y * VGA_WIDTH + x] = (tty_color << 8) | ' ';
        }
    }
}

void tty_set_color(uint8_t fg, uint8_t bg) {
    tty_color = (bg << 4) | (fg & 0x0F);
}

static void tty_put_entry_at(char c, uint8_t color, size_t x, size_t y) {
    tty_buffer[y * VGA_WIDTH + x] = (color << 8) | static_cast<uint8_t>(c);
}

void tty_scroll() {
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            tty_buffer[(y - 1) * VGA_WIDTH + x] = tty_buffer[y * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        tty_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (tty_color << 8) | ' ';
    }
}

static void tty_update_cursor() {
    uint16_t pos = tty_row * VGA_WIDTH + tty_col;
    port_byte_out(0x3D4, 0x0F);
    port_byte_out(0x3D5, static_cast<uint8_t>(pos & 0xFF));
    port_byte_out(0x3D4, 0x0E);
    port_byte_out(0x3D5, static_cast<uint8_t>((pos >> 8) & 0xFF));
}

void tty_putc(char c) {
    serial_putc(c);

    if (c == '\n') {
        tty_col = 0;
        if (++tty_row >= VGA_HEIGHT) {
            tty_scroll();
            tty_row = VGA_HEIGHT - 1;
        }
        tty_update_cursor();
        return;
    }
    if (c == '\t') {
        tty_col = (tty_col + 4) & ~3ULL;
        if (tty_col >= VGA_WIDTH) {
            tty_col = 0;
            tty_row++;
        }
        if (tty_row >= VGA_HEIGHT) {
            tty_scroll();
            tty_row = VGA_HEIGHT - 1;
        }
        tty_update_cursor();
        return;
    }

    tty_put_entry_at(c, tty_color, tty_col, tty_row);
    if (++tty_col >= VGA_WIDTH) {
        tty_col = 0;
        if (++tty_row >= VGA_HEIGHT) {
            tty_scroll();
            tty_row = VGA_HEIGHT - 1;
        }
    }
    tty_update_cursor();
}

void tty_write(const char* str) {
    while (*str) tty_putc(*str++);
}

void tty_write_dec(uint64_t num) {
    if (num == 0) {
        tty_putc('0');
        return;
    }
    char buf[32];
    int i = 30;
    buf[31] = 0;
    while (num > 0) {
        buf[i--] = '0' + (num % 10);
        num /= 10;
    }
    tty_write(&buf[i + 1]);
}

void tty_write_hex(uint64_t num) {
    tty_write("0x");
    if (num == 0) {
        tty_putc('0');
        return;
    }
    char buf[32];
    int i = 30;
    buf[31] = 0;
    while (num > 0) {
        int d = num & 0xF;
        buf[i--] = d < 10 ? '0' + d : 'A' + d - 10;
        num >>= 4;
    }
    tty_write(&buf[i + 1]);
}
