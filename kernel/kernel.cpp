#include "tty.hpp"
#include "gdt.hpp"
#include "idt.hpp"
#include "isr.hpp"
#include "pic.hpp"
#include "timer.hpp"
#include "port.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "kmalloc.hpp"
#include "kb.hpp"
#include "shell.hpp"
#include "task.hpp"
#include "scheduler.hpp"
#include "syscall.hpp"
#include "user.hpp"

static void idle_entry() {
    while (1) {
        __asm__ volatile("hlt");
        scheduler_yield();
    }
}

static void worker1_entry() {
    uint64_t count = 0;
    while (1) {
        tty_write("[W1:");
        tty_write_dec(count++);
        tty_write("] ");
        scheduler_yield();
    }
}

extern "C" uint8_t _binary_hello_start[];
extern "C" uint8_t _binary_hello_end[];

extern "C" {
    extern volatile bool       g_need_resched;
    extern struct Task* volatile g_current_task;
}

extern "C" void kernel_main(uint32_t magic, uint32_t mb_info_addr) {
    tty_init();
    tty_set_color(0x0F, 0x00);

    tty_write("\n");
    tty_write("+------------------------------+\n");
    tty_write("|     MyOS Kernel v0.3.0       |\n");
    tty_write("|     x86_64  C++     Multitask |\n");
    tty_write("+------------------------------+\n\n");

    tty_write("Multiboot2 magic: ");
    tty_write_hex(magic);
    tty_write("\n");
    tty_write("Multiboot info:  ");
    tty_write_hex(mb_info_addr);
    tty_write("\n\n");

    tty_write("[INIT] GDT...\n");  gdt_init();  tty_write("[OK]   GDT installed\n");
    tty_write("[INIT] ISR...\n");  isr_install();  tty_write("[OK]   ISR installed\n");
    tty_write("[INIT] IDT...\n");  idt_init();  tty_write("[OK]   IDT loaded\n");
    tty_write("[INIT] PIC...\n");  pic_init();  pic_set_mask(0xEC, 0xFF);  tty_write("[OK]   PIC remapped\n");
    tty_write("[INIT] PIT...\n");  timer_init(100);  tty_write("[OK]   PIT 100Hz\n");
    tty_write("[INIT] PMM...\n");  pmm_init(128);  tty_write("[OK]   PMM ready\n");
    tty_write("[INIT] VMM...\n");  vmm_init();  tty_write("[OK]   VMM ready\n");
    tty_write("[INIT] Kmalloc...\n");  kmalloc_init();  tty_write("[OK]   Kmalloc ready\n");
    tty_write("[INIT] Keyboard...\n");  kb_init();  tty_write("[OK]   Keyboard ready\n");
    tty_write("[INIT] Syscalls...\n");  syscall_init();  tty_write("[OK]   Syscalls ready\n");
    tty_write("[INIT] Tasks...\n");
    task_init();
    Task* idle = task_create(idle_entry);
    task_create(worker1_entry);
    scheduler_init();
    tty_write("[OK]   Tasks ready\n");

    tty_write("[INIT] TSS...\n");
    tss_init();
    tty_write("[OK]   TSS ready\n");

    tty_write("[INIT] Loading user program...\n");
    UserTaskInfo user_info;
    size_t elf_size = reinterpret_cast<size_t>(_binary_hello_end)
                    - reinterpret_cast<size_t>(_binary_hello_start);
    if (elf_load(_binary_hello_start, elf_size, &user_info)) {
        start_user_task(&user_info);
        tty_write("[OK]   User program loaded\n");
    } else {
        tty_write("[FAIL] User program load failed\n");
    }

    while (port_byte_in(0x3F8 + 5) & 0x01) port_byte_in(0x3F8);

    tty_write("Starting scheduler...\n\n");

    g_current_task = idle;
    idle->state    = TASK_RUNNING;

    __asm__ volatile("sti");

    idle_entry();

    while (1) __asm__ volatile("hlt");
}
