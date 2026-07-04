#include "kmalloc.hpp"
#include "pmm.hpp"
#include "tty.hpp"

struct Block {
    size_t    size;
    Block*    next;
    bool      free;
    uint64_t  magic;
};

#define MAGIC_FREE 0xDEADBEEFCAFEBABEULL
#define MAGIC_USED 0xCAFEBABEDEADBEEFULL
#define HEADER_SIZE (sizeof(Block))
#define MIN_BLOCK_SIZE (HEADER_SIZE + 8)
#define ALIGN8(x) (((x) + 7) & ~7ULL)

static Block* heap_head;
static uint64_t heap_pages;

static void kmalloc_add_pages(int) {
    void* addr = pmm_alloc_page();
    if (!addr) return;
    heap_pages++;

    Block* block = static_cast<Block*>(addr);
    block->size  = 4096 - HEADER_SIZE;
    block->free  = true;
    block->magic = MAGIC_FREE;

    if (!heap_head) {
        block->next = nullptr;
        heap_head = block;
    } else {
        Block* curr = heap_head;
        Block* prev = nullptr;
        while (curr && curr < block) {
            prev = curr;
            curr = curr->next;
        }
        block->next = curr;
        if (prev) prev->next = block;
        else heap_head = block;

        if (prev && prev->free && reinterpret_cast<uint8_t*>(prev) + HEADER_SIZE + prev->size == reinterpret_cast<uint8_t*>(block)) {
            prev->size += HEADER_SIZE + block->size;
            prev->next = block->next;
        }
    }
}

void kmalloc_init() {
    heap_head   = nullptr;
    heap_pages  = 0;
    kmalloc_add_pages(1);
    tty_write("[INIT] Kmalloc... heap started\n");
}

void* kmalloc(size_t size) {
    if (size == 0) return nullptr;
    size = ALIGN8(size);

    Block* curr = heap_head;
    while (curr) {
        if (curr->free && curr->size >= size) {
            if (curr->size >= size + MIN_BLOCK_SIZE + 16) {
                Block* new_block = reinterpret_cast<Block*>(
                    reinterpret_cast<uint8_t*>(curr) + HEADER_SIZE + size);
                new_block->size = curr->size - size - HEADER_SIZE;
                new_block->free = true;
                new_block->next = curr->next;
                new_block->magic = MAGIC_FREE;

                curr->size = size;
                curr->next = new_block;
            }
            curr->free  = false;
            curr->magic = MAGIC_USED;
            return reinterpret_cast<uint8_t*>(curr) + HEADER_SIZE;
        }
        curr = curr->next;
    }

    kmalloc_add_pages(1);
    return kmalloc(size);
}

void kfree(void* ptr) {
    if (!ptr) return;
    Block* block = reinterpret_cast<Block*>(
        reinterpret_cast<uint8_t*>(ptr) - HEADER_SIZE);
    if (block->magic != MAGIC_USED) return;
    block->free  = true;
    block->magic = MAGIC_FREE;

    if (block->next && block->next->free) {
        Block* next = block->next;
        if (reinterpret_cast<uint8_t*>(block) + HEADER_SIZE + block->size == reinterpret_cast<uint8_t*>(next)) {
            block->size += HEADER_SIZE + next->size;
            block->next = next->next;
        }
    }

    Block* curr = heap_head;
    while (curr && curr->next != block) curr = curr->next;
    if (curr && curr->free) {
        if (reinterpret_cast<uint8_t*>(curr) + HEADER_SIZE + curr->size == reinterpret_cast<uint8_t*>(block)) {
            curr->size += HEADER_SIZE + block->size;
            curr->next = block->next;
        }
    }
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    Block* block = reinterpret_cast<Block*>(
        reinterpret_cast<uint8_t*>(ptr) - HEADER_SIZE);
    if (block->magic != MAGIC_USED) return nullptr;
    void* new_ptr = kmalloc(size);
    if (!new_ptr) return nullptr;
    size_t copy_size = block->size < size ? block->size : size;
    for (size_t i = 0; i < copy_size; i++) {
        reinterpret_cast<uint8_t*>(new_ptr)[i] = reinterpret_cast<uint8_t*>(ptr)[i];
    }
    kfree(ptr);
    return new_ptr;
}

void kmalloc_stats() {
    tty_write("Kmalloc: ");
    tty_write_dec(heap_pages);
    tty_write(" pages, blocks: ");
    int n = 0;
    Block* curr = heap_head;
    while (curr) { n++; curr = curr->next; }
    tty_write_dec(n);
    tty_write("\n");
}
