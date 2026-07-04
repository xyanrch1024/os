#include "idt.hpp"

static IDTEntry idt[256];
static IDTPtr  idt_ptr;

extern "C" void idt_flush(uint64_t ptr);

void idt_set_gate(uint8_t vector, uint64_t handler, uint8_t flags) {
    idt[vector].offset_low  = handler & 0xFFFF;
    idt[vector].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[vector].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[vector].selector    = 0x08;
    idt[vector].ist         = 0;
    idt[vector].type_attr   = flags;
    idt[vector].reserved    = 0;
}

void idt_init() {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = reinterpret_cast<uint64_t>(&idt);

    idt_flush(reinterpret_cast<uint64_t>(&idt_ptr));
}
