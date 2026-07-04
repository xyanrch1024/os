#include "syscall.hpp"
#include "isr.hpp"
#include "idt.hpp"
#include "tty.hpp"
#include "task.hpp"
#include "scheduler.hpp"
#include "port.hpp"
#include "fs.hpp"

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

static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    if (fd == FD_STDOUT || fd == FD_STDERR) {
        const char* str = reinterpret_cast<const char*>(buf);
        for (uint64_t i = 0; i < len; i++) {
            tty_putc(str[i]);
        }
        return len;
    }
    return static_cast<uint64_t>(fs_write(static_cast<int>(fd), reinterpret_cast<uint8_t*>(buf), len));
}

static uint64_t sys_getpid() {
    return g_current_task ? g_current_task->pid : 0;
}

static void sys_exit() {
    task_exit();
}

static uint64_t sys_open(uint64_t path, uint64_t flags) {
    return static_cast<uint64_t>(fs_open(reinterpret_cast<const char*>(path), static_cast<int>(flags)));
}

static uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t len) {
    return static_cast<uint64_t>(fs_read(static_cast<int>(fd), reinterpret_cast<uint8_t*>(buf), len));
}

static uint64_t sys_close(uint64_t fd) {
    return static_cast<uint64_t>(fs_close(static_cast<int>(fd)));
}

static uint64_t sys_fstat(uint64_t fd, uint64_t statbuf) {
    return static_cast<uint64_t>(fs_fstat(static_cast<int>(fd), reinterpret_cast<Stat*>(statbuf)));
}

extern "C" void syscall_handler(InterruptFrame* frame) {
    uint64_t num = frame->rax;

    switch (num) {
        case 0:
            sys_yield();
            break;
        case 1:
            frame->rax = sys_write(frame->rdi, frame->rsi, frame->rdx);
            break;
        case 2:
            frame->rax = sys_getpid();
            break;
        case 3:
            sys_exit();
            break;
        case 4:
            frame->rax = sys_open(frame->rdi, frame->rsi);
            break;
        case 5:
            frame->rax = sys_read(frame->rdi, frame->rsi, frame->rdx);
            break;
        case 6:
            frame->rax = sys_close(frame->rdi);
            break;
        case 7:
            frame->rax = sys_fstat(frame->rdi, frame->rsi);
            break;
    }
}

extern "C" void syscall_stub();

void syscall_init() {
    idt_set_gate(0x80, reinterpret_cast<uint64_t>(syscall_stub), 0xEE);
    tty_write("[INIT] Syscalls... int 0x80 ready\n");
}
