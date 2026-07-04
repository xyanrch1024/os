#pragma once

#include "types.hpp"

struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  limit_high_flags;
    uint8_t  base_high;
} __attribute__((packed));

struct GDTPtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void gdt_init();
void gdt_install_tss(uint64_t tss_addr, uint32_t tss_limit);
