#pragma once
#include "types.hpp"

void klog_init();
void klog_write(const char* s);
void klog_write_hex(uint64_t v);
void klog_write_dec(uint64_t v);
void klog_dump();
