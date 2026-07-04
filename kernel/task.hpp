#pragma once

#include "types.hpp"

enum TaskState : uint32_t {
    TASK_READY   = 0,
    TASK_RUNNING = 1,
    TASK_BLOCKED = 2,
    TASK_ZOMBIE  = 3,
};

struct Task {
    uint64_t rsp;
    uint64_t pid;
    uint32_t state;
    void     (*entry)();
    uint64_t kernel_stack;
    Task*    next;
};

void  task_init();
Task* task_create(void (*entry)());
void  task_exit();
void  task_add(Task* task);
