#pragma once

#include "../kernel/types.hpp"

void* memset(void* dest, int val, size_t len);
void* memcpy(void* dest, const void* src, size_t len);
size_t strlen(const char* str);
int strcmp(const char* a, const char* b);
