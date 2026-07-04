#include "pmm.hpp"
#include "tty.hpp"
#include "klog.hpp"

static uint8_t*  bitmap;
static uint64_t  total_pages;
static uint64_t  free_pages;
static uint64_t  bitmap_size;

extern "C" char _kernel_end[];

void pmm_init(uint64_t mem_mb) {
    total_pages = (mem_mb * 1024 * 1024) / 4096;
    bitmap_size = total_pages / 8;

    uint64_t bitmap_addr = (reinterpret_cast<uint64_t>(_kernel_end) + 4095) & ~4095ULL;
    bitmap = reinterpret_cast<uint8_t*>(bitmap_addr);

    for (uint64_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFF;
    }

    free_pages = total_pages;

    uint64_t reserved_end = (bitmap_addr + bitmap_size + 4095) & ~4095ULL;
    uint64_t reserved_pages = reserved_end / 4096;

    for (uint64_t i = 0; i < reserved_pages && i < total_pages; i++) {
        uint64_t byte = i / 8;
        uint8_t  bit  = i % 8;
        bitmap[byte] &= ~(1 << bit);
        free_pages--;
    }

    klog_write("[INIT] PMM... ");
    klog_write_dec(free_pages);
    klog_write(" free pages (");
    klog_write_dec(free_pages * 4 / 1024);
    klog_write(" MB)\n");
}

void* pmm_alloc_page() {
    for (uint64_t i = 0; i < total_pages; i++) {
        uint64_t byte = i / 8;
        uint8_t  bit  = i % 8;
        if (bitmap[byte] & (1 << bit)) {
            bitmap[byte] &= ~(1 << bit);
            free_pages--;
            void* addr = reinterpret_cast<void*>(i * 4096);
            for (int j = 0; j < 4096; j += 8)
                *reinterpret_cast<volatile uint64_t*>(reinterpret_cast<uint64_t>(addr) + j) = 0;
            return addr;
        }
    }
    return nullptr;
}

void pmm_free_page(void* addr) {
    uint64_t page = reinterpret_cast<uint64_t>(addr) / 4096;
    if (page >= total_pages) return;
    uint64_t byte = page / 8;
    uint8_t  bit  = page % 8;
    bitmap[byte] |= (1 << bit);
    free_pages++;
}

uint64_t pmm_free_pages() {
    return free_pages;
}

uint64_t pmm_total_pages() {
    return total_pages;
}
