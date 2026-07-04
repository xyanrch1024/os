#include "gdt.hpp"

static uint8_t gdt_raw[64];
static GDTPtr   gdt_ptr;

extern "C" void gdt_flush(uint64_t ptr);

static void gdt_set_gate(int offset, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t flags) {
    gdt_raw[offset + 0] = limit & 0xFF;
    gdt_raw[offset + 1] = (limit >> 8) & 0xFF;
    gdt_raw[offset + 2] = base & 0xFF;
    gdt_raw[offset + 3] = (base >> 8) & 0xFF;
    gdt_raw[offset + 4] = (base >> 16) & 0xFF;
    gdt_raw[offset + 5] = access;
    gdt_raw[offset + 6] = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    gdt_raw[offset + 7] = (base >> 24) & 0xFF;
}

void gdt_init() {
    for (int i = 0; i < 64; i++) gdt_raw[i] = 0;

    gdt_set_gate(0,  0, 0, 0, 0);                             // null
    gdt_set_gate(8,  0, 0xFFFFFFFF, 0x9A, 0xA0);              // kernel code 64-bit
    gdt_set_gate(16, 0, 0xFFFFFFFF, 0x92, 0x00);              // kernel data
    gdt_set_gate(24, 0, 0xFFFFFFFF, 0xFA, 0xA0);              // user code 64-bit
    gdt_set_gate(32, 0, 0xFFFFFFFF, 0xF2, 0x00);              // user data

    gdt_ptr.limit = 55;
    gdt_ptr.base  = reinterpret_cast<uint64_t>(gdt_raw);

    gdt_flush(reinterpret_cast<uint64_t>(&gdt_ptr));
}

void gdt_install_tss(uint64_t tss_addr, uint32_t tss_limit) {
    int off = 40;

    gdt_raw[off + 0] = tss_limit & 0xFF;
    gdt_raw[off + 1] = (tss_limit >> 8) & 0xFF;
    gdt_raw[off + 2] = tss_addr & 0xFF;
    gdt_raw[off + 3] = (tss_addr >> 8) & 0xFF;
    gdt_raw[off + 4] = (tss_addr >> 16) & 0xFF;
    gdt_raw[off + 5] = 0x89;
    gdt_raw[off + 6] = ((tss_limit >> 16) & 0x0F) | 0x00;
    gdt_raw[off + 7] = (tss_addr >> 24) & 0xFF;
    gdt_raw[off + 8] = (tss_addr >> 32) & 0xFF;
    gdt_raw[off + 9] = (tss_addr >> 40) & 0xFF;
    gdt_raw[off + 10] = (tss_addr >> 48) & 0xFF;
    gdt_raw[off + 11] = (tss_addr >> 56) & 0xFF;
    gdt_raw[off + 12] = 0;
    gdt_raw[off + 13] = 0;
    gdt_raw[off + 14] = 0;
    gdt_raw[off + 15] = 0;

    gdt_ptr.limit = 55;
    gdt_flush(reinterpret_cast<uint64_t>(&gdt_ptr));
}
