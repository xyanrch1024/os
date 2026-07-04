#include "tty.hpp"
#include "klog.hpp"
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
#include "fs.hpp"

static void idle_entry() {
    while (1) {
        __asm__ volatile("hlt");
        scheduler_yield();
    }
}

static void shell_entry() {
    shell_run();
}

extern "C" {
    extern volatile bool       g_need_resched;
    extern struct Task* volatile g_current_task;
}

extern "C" void kernel_main(uint32_t magic, uint32_t mb_info_addr) {
    tty_init();
    tty_set_color(0x0F, 0x00);
    klog_init();

    klog_write("\n");
    klog_write("+------------------------------+\n");
    klog_write("|     MyOS Kernel v0.3.0       |\n");
    klog_write("|     x86_64  C++     Multitask |\n");
    klog_write("+------------------------------+\n\n");

    klog_write("Multiboot2 magic: ");
    klog_write_hex(magic);
    klog_write("\n");
    klog_write("Multiboot info:  ");
    klog_write_hex(mb_info_addr);
    klog_write("\n\n");

    klog_write("[INIT] GDT...\n");  gdt_init();  klog_ok("[OK]   GDT installed\n");
    klog_write("[INIT] ISR...\n");  isr_install();  klog_ok("[OK]   ISR installed\n");
    klog_write("[INIT] IDT...\n");  idt_init();  klog_ok("[OK]   IDT loaded\n");
    klog_write("[INIT] PIC...\n");  pic_init();  pic_set_mask(0xEC, 0xFF);  klog_ok("[OK]   PIC remapped\n");
    klog_write("[INIT] PIT...\n");  timer_init(100);  klog_ok("[OK]   PIT 100Hz\n");
    klog_write("[INIT] PMM...\n");  pmm_init(128);  klog_ok("[OK]   PMM ready\n");
    klog_write("[INIT] VMM...\n");  vmm_init();  klog_ok("[OK]   VMM ready\n");
    klog_write("[INIT] Kmalloc...\n");  kmalloc_init();  klog_ok("[OK]   Kmalloc ready\n");
    klog_write("[INIT] Keyboard...\n");  kb_init();  klog_ok("[OK]   Keyboard ready\n");
    klog_write("[INIT] Syscalls...\n");  syscall_init();  klog_ok("[OK]   Syscalls ready\n");
    klog_write("[INIT] Tasks...\n");
    task_init();
    Task* idle = task_create(idle_entry);
    task_create(shell_entry);
    scheduler_init();
    klog_ok("[OK]   Tasks ready\n");

    klog_write("[INIT] TSS...\n");
    tss_init();
    klog_ok("[OK]   TSS ready\n");

    klog_write("[INIT] Filesystem...\n");
    fs_init();
    klog_ok("[OK]   Filesystem ready\n");

    while (port_byte_in(0x3F8 + 5) & 0x01) port_byte_in(0x3F8);

    klog_write("Starting scheduler...\n\n");

    g_current_task = idle;
    idle->state    = TASK_RUNNING;

    __asm__ volatile("sti");

    idle_entry();

    while (1) __asm__ volatile("hlt");
}
