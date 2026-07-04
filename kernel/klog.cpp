#include "klog.hpp"
#include "tty.hpp"

#define KLOG_BUF_SIZE 4096

static char    klog_buf[KLOG_BUF_SIZE];
static uint32_t klog_pos;
static bool     klog_wrapped;

void klog_init() {
    klog_pos = 0;
    klog_wrapped = false;
    klog_buf[0] = 0;
}

static void buf_write(const char* s) {
    while (*s) {
        if (klog_pos >= KLOG_BUF_SIZE - 1) {
            klog_pos = 0;
            klog_wrapped = true;
        }
        klog_buf[klog_pos++] = *s++;
    }
    if (klog_pos < KLOG_BUF_SIZE) {
        klog_buf[klog_pos] = 0;
    }
}

void klog_write(const char* s) {
    tty_write(s);
    buf_write(s);
}

void klog_write_hex(uint64_t v) {
    tty_write_hex(v);
    buf_write("0x");
    if (v == 0) {
        buf_write("0");
        return;
    }
    char buf[32];
    int i = 30;
    buf[31] = 0;
    while (v > 0) {
        int d = v & 0xF;
        buf[i--] = d < 10 ? '0' + d : 'A' + d - 10;
        v >>= 4;
    }
    buf_write(&buf[i + 1]);
}

void klog_write_dec(uint64_t v) {
    tty_write_dec(v);
    char buf[32];
    int i = 30;
    buf[31] = 0;
    if (v == 0) {
        buf[i--] = '0';
    } else {
        while (v > 0) {
            buf[i--] = '0' + (v % 10);
            v /= 10;
        }
    }
    buf_write(&buf[i + 1]);
}

void klog_dump() {
    if (klog_wrapped) {
        tty_write(&klog_buf[klog_pos]);
    }
    klog_buf[klog_pos] = 0;
    tty_write(klog_buf);
}
