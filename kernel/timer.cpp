#include "timer.hpp"
#include "port.hpp"
#include "isr.hpp"
#include "pic.hpp"
#include "scheduler.hpp"

static uint64_t tick_count = 0;

void timer_init(uint32_t freq) {
    uint32_t divisor = 1193180 / freq;

    port_byte_out(0x43, 0x36);

    port_byte_out(0x40, static_cast<uint8_t>(divisor & 0xFF));
    port_byte_out(0x40, static_cast<uint8_t>((divisor >> 8) & 0xFF));
}

uint64_t timer_get_ticks() {
    return tick_count;
}

extern "C" void irq_timer_handler(InterruptFrame*) {
    tick_count++;
    scheduler_tick();
}
