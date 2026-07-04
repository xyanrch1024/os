#include "libc.hpp"

void* memset(void* dest, int val, size_t len) {
    auto* p = static_cast<unsigned char*>(dest);
    for (size_t i = 0; i < len; i++) p[i] = static_cast<unsigned char>(val);
    return dest;
}

void* memcpy(void* dest, const void* src, size_t len) {
    auto* d = static_cast<unsigned char*>(dest);
    auto* s = static_cast<const unsigned char*>(src);
    for (size_t i = 0; i < len; i++) d[i] = s[i];
    return dest;
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *reinterpret_cast<const unsigned char*>(a)
         - *reinterpret_cast<const unsigned char*>(b);
}
