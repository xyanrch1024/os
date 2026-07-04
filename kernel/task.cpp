#include "task.hpp"
#include "pmm.hpp"
#include "kmalloc.hpp"
#include "tty.hpp"
#include "klog.hpp"

static uint64_t next_pid = 1;

extern "C" {
    volatile bool        g_need_resched   = false;
    struct Task* volatile g_current_task  = nullptr;
}

static Task* task_list_head = nullptr;

void task_add(Task* task) {
    if (!task_list_head) {
        task_list_head = task;
        task->next = task;
    } else {
        task->next = task_list_head;
        Task* t = task_list_head;
        while (t->next != task_list_head) t = t->next;
        t->next = task;
        task_list_head = task;
    }
}

extern "C" void switch_to(uint64_t* current_rsp, uint64_t* next_rsp);

static void task_entry_wrapper() {
    if (g_current_task && g_current_task->entry) {
        g_current_task->entry();
    }
    while (1) __asm__ volatile("hlt");
}

void task_init() {
    next_pid = 1;
    g_current_task = nullptr;
    task_list_head = nullptr;
    klog_write("[INIT] Task system...\n");
}

Task* task_create(void (*entry)()) {
    void* stack_page = pmm_alloc_page();
    if (!stack_page) return nullptr;

    Task* task = reinterpret_cast<Task*>(kmalloc(sizeof(Task)));
    if (!task) {
        pmm_free_page(stack_page);
        return nullptr;
    }

    task->pid           = next_pid++;
    task->state         = TASK_READY;
    task->kernel_stack  = reinterpret_cast<uint64_t>(stack_page);
    task->entry         = entry;
    task->ticks_left    = 3;

    uint64_t* sp = reinterpret_cast<uint64_t*>(task->kernel_stack + 4096);

    *--sp = reinterpret_cast<uint64_t>(task_entry_wrapper);
    *--sp = 0x202;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;

    task->rsp = reinterpret_cast<uint64_t>(sp);

    task_add(task);

    klog_write("  Task ");
    klog_write_dec(task->pid);
    klog_write(" created\n");
    return task;
}

void task_exit() {
    g_current_task->state = TaskState(3);
    while (1) __asm__ volatile("hlt");
}
