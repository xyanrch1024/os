#pragma once

#include "types.hpp"

void  pmm_init(uint64_t mem_mb);
void* pmm_alloc_page();
void  pmm_free_page(void* addr);
uint64_t pmm_free_pages();
uint64_t pmm_total_pages();
