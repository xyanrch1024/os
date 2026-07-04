#pragma once

#include "types.hpp"

void  kmalloc_init();
void* kmalloc(size_t size);
void  kfree(void* ptr);
void* krealloc(void* ptr, size_t size);
void  kmalloc_stats();
