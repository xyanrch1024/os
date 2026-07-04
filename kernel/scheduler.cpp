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

#define TIME_SLICE_DEFAULT 3

void scheduler_tick() {
    if (!g_current_task) return;
    if (g_current_task->ticks_left > 0) {
        g_current_task->ticks_left--;
    }
    if (g_current_task->ticks_left == 0) {
        g_need_resched = true;
    }
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
    next->ticks_left = TIME_SLICE_DEFAULT;
    g_current_task = next;

    switch_to(&prev->rsp, &next->rsp);
}
