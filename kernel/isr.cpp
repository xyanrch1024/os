#include "isr.hpp"
#include "idt.hpp"
#include "tty.hpp"
#include "port.hpp"
#include "scheduler.hpp"

static const char* exception_names[] = {
    "Division by zero",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack segment fault",
    "General protection fault",
    "Page fault",
    "Reserved",
    "x87 FPU error",
    "Alignment check",
    "Machine check",
    "SIMD FP exception",
    "Virtualization exception",
    "Control protection exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor injection",
    "VMM communication",
    "Security exception",
    "Reserved",
};

static void dump_regs(InterruptFrame* frame) {
    tty_write("  RAX="); tty_write_hex(frame->rax);
    tty_write(" RBX="); tty_write_hex(frame->rbx);
    tty_write(" RCX="); tty_write_hex(frame->rcx);
    tty_write(" RDX="); tty_write_hex(frame->rdx);
    tty_write("\n");
    tty_write("  RSI="); tty_write_hex(frame->rsi);
    tty_write(" RDI="); tty_write_hex(frame->rdi);
    tty_write(" RBP="); tty_write_hex(frame->rbp);
    tty_write("\n");
    tty_write("  RIP="); tty_write_hex(frame->rip);
    tty_write(" CS="); tty_write_hex(frame->cs);
    tty_write(" RFLAGS="); tty_write_hex(frame->rflags);
    tty_write("\n");
}

extern "C" void irq_timer_handler(InterruptFrame*);
extern "C" void kb_irq_handler();
extern "C" void serial_rx_irq_handler();
extern "C" void syscall_handler(InterruptFrame*);

extern volatile bool g_need_resched;

extern "C" void isr_handler(InterruptFrame* frame) {
    if (frame->int_no < 32) {
        tty_set_color(0x0C, 0x00);
        tty_write("\n=== EXCEPTION: ");
        tty_write(exception_names[frame->int_no]);
        tty_write(" (int ");
        tty_write_dec(frame->int_no);
        tty_write(", err ");
        tty_write_hex(frame->error_code);
        tty_write(") ===\n");

        if (frame->int_no == 14) {
            uint64_t cr2;
            __asm__("mov %%cr2, %0" : "=r"(cr2));
            tty_write("  CR2 (fault address): ");
            tty_write_hex(cr2);
            tty_write("\n");
            tty_write("  Error: ");
            if (frame->error_code & 1) tty_write("P ");
            if (frame->error_code & 2) tty_write("W ");
            if (frame->error_code & 4) tty_write("U ");
            if (frame->error_code & 8) tty_write("RSVD ");
            if (frame->error_code & 16) tty_write("IF ");
            tty_write("\n");
        }

        dump_regs(frame);
        tty_set_color(0x0F, 0x00);
        while (1) { __asm__ volatile("hlt"); }
    }

    if (frame->int_no == 32) {
        irq_timer_handler(frame);
    } else if (frame->int_no == 33) {
        kb_irq_handler();
    } else if (frame->int_no == 36) {
        serial_rx_irq_handler();
    } else if (frame->int_no == 0x80) {
        syscall_handler(frame);
    }

    if (frame->int_no >= 32 && frame->int_no < 48) {
        if (frame->int_no >= 40) {
            port_byte_out(0xA0, 0x20);
        }
        port_byte_out(0x20, 0x20);
    }

    if (g_need_resched) {
        g_need_resched = false;
        scheduler_yield();
    }
}

void isr_install() {
    extern uint64_t isr_stubs[];

    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, isr_stubs[i], 0x8E);
    }

    for (int i = 32; i < 48; i++) {
        idt_set_gate(i, isr_stubs[i], 0x8E);
    }

    for (int i = 48; i < 256; i++) {
        idt_set_gate(i, isr_stubs[i], 0x8E);
    }
}
