#include "scheduler.hpp"
#include "task.hpp"
#include "tty.hpp"

extern "C" {
    extern volatile bool       g_need_resched;
    extern struct Task* volatile g_current_task;
}

extern "C" void switch_to(uint64_t* current_rsp, uint64_t* next_rsp);

void scheduler_init() {
    tty_write("[INIT] Scheduler... Round Robin\n");
}

void scheduler_tick() {
}

void scheduler_yield() {
    if (!g_current_task) return;

    Task* start = g_current_task;
    Task* next  = start->next;
    while (next != start && next->state != TASK_READY) {
        next = next->next;
    }

    if (next == start || next->state != TASK_READY) return;

    Task* prev = g_current_task;
    prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    g_current_task = next;

    switch_to(&prev->rsp, &next->rsp);
}
