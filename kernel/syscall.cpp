#include "syscall.hpp"
#include "isr.hpp"
#include "idt.hpp"
#include "tty.hpp"
#include "task.hpp"
#include "scheduler.hpp"
#include "port.hpp"

extern "C" {
    extern struct Task* volatile g_current_task;
}

__asm__(
    ".globl syscall_stub\n"
    "syscall_stub:\n"
    "  cli\n"
    "  pushq $0\n"
    "  pushq $0x80\n"
    "  jmp isr_common\n"
);

static void sys_yield() {
    scheduler_yield();
}

static void sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    if (fd == 1) {
        const char* str = reinterpret_cast<const char*>(buf);
        for (uint64_t i = 0; i < len; i++) {
            tty_putc(str[i]);
        }
    }
}

static uint64_t sys_getpid() {
    return g_current_task ? g_current_task->pid : 0;
}

static void sys_exit() {
    task_exit();
}

extern "C" void syscall_handler(InterruptFrame* frame) {
    uint64_t num = frame->rax;

    switch (num) {
        case 0:
            sys_yield();
            break;
        case 1:
            sys_write(frame->rdi, frame->rsi, frame->rdx);
            break;
        case 2:
            frame->rax = sys_getpid();
            break;
        case 3:
            sys_exit();
            break;
    }
}

extern "C" void syscall_stub();

void syscall_init() {
    idt_set_gate(0x80, reinterpret_cast<uint64_t>(syscall_stub), 0xEE);
    tty_write("[INIT] Syscalls... int 0x80 ready\n");
}
