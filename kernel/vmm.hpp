#pragma once

#include "types.hpp"

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)

void  vmm_init();
void  vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void  vmm_unmap_page(uint64_t virt);
bool  vmm_is_mapped(uint64_t virt);
void  vmm_tlb_flush();
