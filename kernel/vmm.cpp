#include "vmm.hpp"
#include "pmm.hpp"
#include "tty.hpp"
#include "klog.hpp"

#define PAGE_WRITETHROUGH (1ULL << 3)
#define PAGE_CACHE_DISABLE (1ULL << 4)
#define PAGE_ACCESSED (1ULL << 5)
#define PAGE_DIRTY    (1ULL << 6)
#define PAGE_HUGE     (1ULL << 7)
#define PAGE_GLOBAL   (1ULL << 8)

static uint64_t* pml4;

static uint64_t* get_cr3() {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return reinterpret_cast<uint64_t*>(cr3 & ~0xFFFULL);
}

void vmm_tlb_flush() {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

static void vmm_invlpg(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_init() {
    void* pml4_page = pmm_alloc_page();
    void* pdpt_page = pmm_alloc_page();
    void* pd_page   = pmm_alloc_page();

    pml4 = reinterpret_cast<uint64_t*>(pml4_page);
    uint64_t* pdpt = reinterpret_cast<uint64_t*>(pdpt_page);
    uint64_t* pd   = reinterpret_cast<uint64_t*>(pd_page);

    for (int i = 0; i < 512; i++) pml4[i] = 0;
    for (int i = 0; i < 512; i++) pdpt[i] = 0;
    for (int i = 0; i < 512; i++) pd[i]   = 0;

    pml4[0] = reinterpret_cast<uint64_t>(pdpt_page) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    pdpt[0] = reinterpret_cast<uint64_t>(pd_page) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

    for (int i = 0; i < 64; i++) {
        pd[i] = (static_cast<uint64_t>(i) << 21) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_HUGE;
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_page) : "memory");

    klog_ok("[OK]   VMM: identity-mapped 128MB\n");
}

static uint64_t* walk_page_table(uint64_t virt, bool create) {
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdpt_idx = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        if (!create) return nullptr;
        void* page = pmm_alloc_page();
        pml4[pml4_idx] = reinterpret_cast<uint64_t>(page) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }

    uint64_t* pdpt = reinterpret_cast<uint64_t*>(pml4[pml4_idx] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        if (!create) return nullptr;
        void* page = pmm_alloc_page();
        pdpt[pdpt_idx] = reinterpret_cast<uint64_t>(page) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }

    uint64_t* pd = reinterpret_cast<uint64_t*>(pdpt[pdpt_idx] & ~0xFFFULL);

    if (pd[pd_idx] & PAGE_HUGE) {
        if (!create) return nullptr;
        void* pt_page = pmm_alloc_page();
        uint64_t* new_pt = reinterpret_cast<uint64_t*>(pt_page);

        uint64_t old_flags = pd[pd_idx] & (0xFFF | ~0x1FFFFFULL);
        uint64_t base = pd[pd_idx] & ~0x1FFFFFULL;
        for (int i = 0; i < 512; i++) {
            new_pt[i] = (base + i * 4096) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        }

        pd[pd_idx] = reinterpret_cast<uint64_t>(pt_page) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }

    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        if (!create) return nullptr;
        void* page = pmm_alloc_page();
        pd[pd_idx] = reinterpret_cast<uint64_t>(page) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }

    uint64_t* pt = reinterpret_cast<uint64_t*>(pd[pd_idx] & ~0xFFFULL);
    if (pt[pt_idx] & PAGE_PRESENT) {
        return &pt[pt_idx];
    }

    if (!create) return nullptr;
    return &pt[pt_idx];
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t* pte = walk_page_table(virt, true);
    if (pte) {
        *pte = (phys & ~0xFFFULL) | (flags & 0xFFF) | PAGE_PRESENT;
        vmm_invlpg(virt);
    }
}

void vmm_unmap_page(uint64_t virt) {
    uint64_t* pte = walk_page_table(virt, false);
    if (pte) {
        *pte = 0;
        vmm_invlpg(virt);
    }
}

bool vmm_is_mapped(uint64_t virt) {
    uint64_t* pte = walk_page_table(virt, false);
    return pte && (*pte & PAGE_PRESENT);
}
