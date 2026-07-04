#include "../kernel/kmalloc.hpp"

void* operator new(size_t size) {
    return kmalloc(size);
}

void* operator new[](size_t size) {
    return kmalloc(size);
}

void operator delete(void* ptr) noexcept {
    kfree(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    kfree(ptr);
}

void operator delete[](void* ptr) noexcept {
    kfree(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    kfree(ptr);
}

extern "C" {

void __cxa_pure_virtual() {
    while (1) __asm__ volatile("hlt");
}

int __cxa_atexit(void (*)(void*), void*, void*) {
    return 0;
}

void __cxa_guard_acquire() {}
void __cxa_guard_release() {}

}
