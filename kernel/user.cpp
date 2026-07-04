#include "user.hpp"
#include "gdt.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "kmalloc.hpp"
#include "tty.hpp"
#include "task.hpp"
#include "libc.hpp"

extern "C" void enter_usermode(uint64_t entry, uint64_t user_rsp);
extern "C" void tss_flush();

struct TSS {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static TSS tss __attribute__((aligned(16)));
static uint64_t g_next_user_pid = 100;

void tss_init() {
    memset(&tss, 0, sizeof(tss));
    gdt_install_tss(reinterpret_cast<uint64_t>(&tss), sizeof(tss) - 1);
    tss_flush();
    tty_write("[INIT] TSS ready\n");
}

void set_user_kernel_stack(uint64_t rsp0) {
    tss.rsp[0] = rsp0;
}

static UserTaskInfo* g_user_info;

extern "C" void user_kernel_bootstrap() {
    if (!g_user_info) while (1) __asm__("hlt");
    set_user_kernel_stack(reinterpret_cast<uint64_t>(pmm_alloc_page()) + 0x1000);
    enter_usermode(g_user_info->entry, g_user_info->user_stack_top);
    while (1) __asm__("hlt");
}

bool elf_load(const uint8_t* data, size_t size, UserTaskInfo* info) {
    if (size < sizeof(Elf64_Ehdr)) return false;
    const Elf64_Ehdr* ehdr = reinterpret_cast<const Elf64_Ehdr*>(data);

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        tty_write("ELF: bad magic\n");
        return false;
    }
    if (ehdr->e_ident[4] != 2) { tty_write("ELF: not 64-bit\n"); return false; }
    if (ehdr->e_machine != 0x3E) { tty_write("ELF: not x86_64\n"); return false; }

    info->entry = ehdr->e_entry;
    tty_write("ELF: entry=0x");
    tty_write_hex(info->entry);
    tty_write("\n");

    const Elf64_Phdr* phdrs = reinterpret_cast<const Elf64_Phdr*>(data + ehdr->e_phoff);
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type != 1) continue;

        uint64_t vaddr  = phdrs[i].p_vaddr;
        uint64_t memsz  = phdrs[i].p_memsz;
        uint64_t filesz = phdrs[i].p_filesz;
        uint64_t offset = phdrs[i].p_offset;
        uint64_t flags  = phdrs[i].p_flags;

        uint64_t page_start = vaddr & ~0xFFFULL;
        uint64_t page_end   = (vaddr + memsz + 0xFFF) & ~0xFFFULL;

        for (uint64_t page = page_start; page < page_end; page += 0x1000) {
            void* pg = pmm_alloc_page();
            if (!pg) {
                tty_write("ELF: OOM\n");
                return false;
            }
            uint64_t pg_flags = PAGE_PRESENT | PAGE_USER;
            if (flags & 2) pg_flags |= PAGE_WRITABLE;
            vmm_map_page(page, reinterpret_cast<uint64_t>(pg), pg_flags);
        }

        uint8_t* dest = reinterpret_cast<uint8_t*>(vaddr);
        for (uint64_t j = 0; j < filesz; j++) dest[j] = data[offset + j];
        for (uint64_t j = filesz; j < memsz; j++) dest[j] = 0;

        tty_write("  LOAD 0x");
        tty_write_hex(vaddr);
        tty_write(" size=0x");
        tty_write_hex(memsz);
        tty_write("\n");
    }

    vmm_tlb_flush();
    return true;
}

void start_user_task(UserTaskInfo* info) {
    g_user_info = info;

    void* ustack = pmm_alloc_page();
    if (!ustack) return;
    info->user_stack_top = reinterpret_cast<uint64_t>(ustack) + 0x1000;

    void* kstack = pmm_alloc_page();
    if (!kstack) return;
    uint64_t kstack_top = reinterpret_cast<uint64_t>(kstack) + 0x1000;

    set_user_kernel_stack(kstack_top);

    uint64_t* sp = reinterpret_cast<uint64_t*>(kstack_top);
    *--sp = reinterpret_cast<uint64_t>(info);
    *--sp = reinterpret_cast<uint64_t>(user_kernel_bootstrap);
    *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0; *--sp = 0; *--sp = 0;

    Task* task = reinterpret_cast<Task*>(kmalloc(sizeof(Task)));
    task->rsp           = reinterpret_cast<uint64_t>(sp);
    task->pid           = g_next_user_pid++;
    task->state         = TASK_READY;
    task->entry         = nullptr;
    task->kernel_stack  = reinterpret_cast<uint64_t>(kstack);
    task->next          = nullptr;
    task->ticks_left    = 3;

    tty_write("  User task PID=");
    tty_write_dec(task->pid);
    tty_write(" created\n");

    task_add(task);
}
