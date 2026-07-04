# 进程管理与任务调度

## 1. 整体架构

```
┌────────────────────────────────────────────────┐
│             用户程序 (Ring 3)                    │
│  hello.S : int 0x80 系统调用                     │
└──────────────────┬─────────────────────────────┘
                   │ int 0x80
┌──────────────────▼─────────────────────────────┐
│             系统调用层 (syscall.cpp)              │
│  syscall_stub → isr_common → syscall_handler     │
│  系统调用表: yield/write/getpid/exit/open/read... │
└──────────────────┬─────────────────────────────┘
                   │
┌──────────────────▼─────────────────────────────┐
│         调度器 (scheduler.cpp)                   │
│  cooperative round-robin, scheduler_yield()      │
└──────────────────┬─────────────────────────────┘
                   │ switch_to()
┌──────────────────▼─────────────────────────────┐
│         任务系统 (task.cpp)                      │
│  Task 结构体, 环形链表, task_create, task_exit   │
└──────────────────┬─────────────────────────────┘
                   │
┌──────────────────▼─────────────────────────────┐
│         上下文切换 (switch.S)                    │
│  保存/恢复 callee-saved 寄存器                   │
└────────────────────────────────────────────────┘
```

## 2. 任务系统 (Task)

### 2.1 Task 结构体

```cpp
struct Task {
    uint64_t rsp;           // 内核栈指针 (切换时保存)
    uint64_t pid;           // 进程 ID
    uint32_t state;         // 任务状态
    void     (*entry)();    // 入口函数指针
    uint64_t kernel_stack;  // 内核栈基地址 (4KB)
    Task*    next;          // 链表下一节点
};
```

### 2.2 任务状态

| 状态 | 值 | 说明 |
|------|----|------|
| `TASK_READY` | 0 | 就绪，等待调度 |
| `TASK_RUNNING` | 1 | 正在运行 |
| `TASK_BLOCKED` | 2 | 阻塞 (当前未使用) |
| `TASK_ZOMBIE` | 3 | 已退出 |

### 2.3 任务链表

所有 Task 通过 `next` 指针构成**环形链表**：

```
          ┌──────────────────────────────────────┐
          │                                       │
    [Task A] → [Task B] → [Task C] → [Task D] ──┘
      ↑                                              (环回)
      task_list_head (指向最新创建的任务)
```

`task_add()` 将新任务插入到 `task_list_head` 后面：

```cpp
void task_add(Task* task) {
    if (!task_list_head) {
        task_list_head = task;
        task->next = task;           // 自环
    } else {
        task->next = task_list_head;
        Task* t = task_list_head;
        while (t->next != task_list_head) t = t->next;
        t->next = task;
        task_list_head = task;
    }
}
```

### 2.4 任务创建 (`task_create`)

```cpp
Task* task_create(void (*entry)())
```

步骤：
1. 分配一个 4KB 物理页作为内核栈 (`kernel_stack`)
2. 分配一个 `Task` 结构体 (kmalloc)
3. 初始化 PID、状态为 `TASK_READY`
4. 在栈上构建**上下文切换帧**：

```
内核栈 (4KB) 布局:
                                                      ← kernel_stack + 0x1000 (栈顶)
  ┌─────────────────────────────┐
  │ task_entry_wrapper          │ ← sp 初始指向这里 (作为 ret 地址)
  ├─────────────────────────────┤
  │ 0x0 (rbx)                   │
  │ 0x0 (rbp)                   │
  │ 0x0 (r12)                   │
  │ 0x0 (r13)                   │
  │ 0x0 (r14)                   │
  │ 0x0 (r15)                   │
  ├─────────────────────────────┤
  │ ...                         │
  │                             │
  └─────────────────────────────┘
                                                      ← kernel_stack (栈底, 4KB对齐)
```

5. `task->rsp` 指向 callee-saved 区顶部（即压入 `task_entry_wrapper` 后的栈指针）
6. 将任务加入环形链表

### 2.5 任务入口包装 (`task_entry_wrapper`)

```cpp
static void task_entry_wrapper() {
    if (g_current_task && g_current_task->entry) {
        g_current_task->entry();  // 调用任务的入口函数
    }
    while (1) __asm__ volatile("hlt");  // 入口函数返回后 hlt 停机
}
```

这是当 `switch_to` 返回切换到新任务时，`ret` 指令弹出 `task_entry_wrapper` 地址，然后在此调用任务真正的 `entry()  `。

### 2.6 任务退出 (`task_exit`)

```cpp
void task_exit() {
    g_current_task->state = TASK_ZOMBIE;
    while (1) __asm__ volatile("hlt");
}
```

标记为僵尸后 hlt，依赖调度器切换出去（实际退出后不再被调度，因为 `scheduler_yield` 只选 `TASK_READY` 的任务）。

---

## 3. 上下文切换 (switch.S)

### 3.1 `switch_to(current_rsp_ptr, next_rsp_ptr)`

```
C 原型: switch_to(uint64_t* current_rsp, uint64_t* next_rsp)

RDI = &g_current_task->rsp   (保存当前上下文的地址)
RSI = &next_task->rsp        (加载新上下文的地址)
```

完整流程：

```asm
switch_to:
    ; 1. 保存 callee-saved 寄存器到当前栈
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    ; 2. 保存当前 RSP 到 *current_rsp (RDI)
    movq %rsp, (%rdi)

    ; 3. 从 *next_rsp 恢复新的 RSP
    movq (%rsi), %rsp

    ; 4. 恢复 callee-saved 寄存器
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    ; 5. ret → 继续在新任务的上下文中执行
    ret
```

### 3.2 寄存器约定

x86_64 System V ABI 下，`switch_to` 作为普通函数调用，只需保存/恢复 **callee-saved** 寄存器：
- `rbx`, `rbp`, `r12`, `r13`, `r14`, `r15`

Caller-saved 寄存器 (`rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`-`r11`) 由调用者负责。

---

## 4. 调度器 (Scheduler)

### 4.1 初始化

```cpp
void scheduler_init() {
    tty_write("[INIT] Scheduler... Round Robin\n");
}
```

仅打印信息，不做额外设置。

### 4.2 时间片 (`scheduler_tick`)

```cpp
void scheduler_tick() {
    // 当前为空: 预留用于未来抢占式调度
}
```

PIT 每 10ms (100Hz) 触发 IRQ0 → `timer_handler` → `scheduler_tick()`，但目前不执行抢占。将来可实现：`g_need_resched = true`，在 ISR 末检查并触发切换。

### 4.3 主动让出 (`scheduler_yield`)

```cpp
void scheduler_yield() {
    if (!g_current_task) return;

    // 从当前任务开始，环形遍历找到下一个 READY 任务
    Task* start = g_current_task;
    Task* next  = start->next;
    while (next != start && next->state != TASK_READY) {
        next = next->next;
    }

    // 如果没有其他 READY 任务，不切换
    if (next == start || next->state != TASK_READY) return;

    // 切换
    Task* prev = g_current_task;
    prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    g_current_task = next;

    switch_to(&prev->rsp, &next->rsp);
}
```

**调度策略**：**协作式 Round-Robin**
- 从当前任务开始遍历环形链表
- 选择第一个 `TASK_READY` 的任务（当前任务被切出去时已标记为 READY）
- 如果没有任何 READY 任务，保持当前任务继续执行

**关键限制**：
- 任务必须主动调用 `scheduler_yield()` 才能让出 CPU
- 没有时间片抢占，纯协作式
- idle 任务循环 `hlt; scheduler_yield()` 在无事可做时休眠并尝试让出

---

## 5. 系统调用

### 5.1 系统调用入口 (`syscall_stub`)

```asm
__asm__(
    ".globl syscall_stub\n"
    "syscall_stub:\n"
    "  cli\n"
    "  pushq $0\n"          ; 错误码 = 0 (模拟 ISR_NOERR)
    "  pushq $0x80\n"       ; int_no = 0x80
    "  jmp isr_common\n"    ; 复用 ISR 中断处理框架
);
```

IDT 第 0x80 号门设置为 `0xEE`（DPL=3，Present，Interrupt Gate），允许 ring 3 触发。

### 5.2 中断帧 (InterruptFrame)

从 `isr_common` 进入 `isr_handler` 时，栈上布局为：

```
RSP → ┌──────────────┐
      │ r15          │ ← pushq %r15  (isr_common)
      │ r14          │
      │ ...          │
      │ rax          │
      ├──────────────┤
      │ int_no = 128 │ ← syscall_stub pushq $0x80
      │ error_code=0 │ ← syscall_stub pushq $0
      ├──────────────┤
      │ rip          │ ← iretq 帧
      │ cs           │
      │ rflags       │
      │ rsp          │
      │ ss           │
      └──────────────┘
```

### 5.3 系统调用分派 (`syscall_handler`)

系统调用号通过 `rax` 传递，参数通过 `rdi, rsi, rdx` 传递，返回值放回 `rax`：

```cpp
extern "C" void syscall_handler(InterruptFrame* frame) {
    uint64_t num = frame->rax;
    switch (num) {
        case 0: sys_yield();            break;
        case 1: frame->rax = sys_write(frame->rdi, frame->rsi, frame->rdx); break;
        case 2: frame->rax = sys_getpid(); break;
        case 3: sys_exit();             break;
        case 4: frame->rax = sys_open(...);   break;
        case 5: frame->rax = sys_read(...);   break;
        case 6: frame->rax = sys_close(...);  break;
        case 7: frame->rax = sys_fstat(...);  break;
    }
}
```

### 5.4 系统调用表

| 号 | 函数 | 参数 | 说明 |
|----|------|------|------|
| 0 | `yield` | — | 主动让出 CPU |
| 1 | `write` | fd, buf, len | 写文件/终端 |
| 2 | `getpid` | — | 获取进程 PID |
| 3 | `exit` | — | 退出当前进程 |
| 4 | `open` | path, flags | 打开文件 |
| 5 | `read` | fd, buf, len | 读文件 |
| 6 | `close` | fd | 关闭文件 |
| 7 | `fstat` | fd, statbuf | 获取文件状态 |

### 5.5 用户程序调用示例 (`hello.S`)

```asm
_start:
    movq $2, %rax       ; syscall 2 = getpid
    int $0x80

    movq $1, %rax       ; syscall 1 = write
    movq $1, %rdi       ; fd = 1 (stdout)
    leaq msg(%rip), %rsi ; buf = "Hello from userspace!\n"
    movq $len, %rdx     ; len
    int $0x80

    movq $0, %rax       ; syscall 0 = yield
    int $0x80

    jmp _start          ; 循环
```

---

## 6. 用户模式 (TSS + Ring 3)

### 6.1 TSS (Task State Segment)

TSS 用于硬件在 ring 0→3/3→0 切换时自动加载 `RSP0`：

```cpp
struct TSS {
    uint32_t reserved0;
    uint64_t rsp[3];      // rsp[0] = 内核栈指针 (ring 0)
    uint64_t reserved1;
    uint64_t ist[7];      // Interrupt Stack Table (未使用)
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));
```

### 6.2 GDT 中的 TSS 描述符

TSS 使用 **16 字节**描述符（GDT 第 40-55 字节）：

```
GDT 布局:
  [0-7]     null (必须)
  [8-15]    kernel code (0x9A, 0xA0)   → 选择子 0x08
  [16-23]   kernel data (0x92, 0x00)   → 选择子 0x10
  [24-31]   user   code (0xFA, 0xA0)   → 选择子 0x1B (RPL=3)
  [32-39]   user   data (0xF2, 0x00)   → 选择子 0x23 (RPL=3)
  [40-55]   TSS 描述符 (16 字节)        → 选择子 0x28
```

`gdt_install_tss()` 写入 TSS 基地址（64 位）和界限（limit），access 字节为 `0x89`（Present, 64-bit TSS available）。

### 6.3 TSS 初始化

```cpp
void tss_init() {
    memset(&tss, 0, sizeof(tss));
    gdt_install_tss(reinterpret_cast<uint64_t>(&tss), sizeof(tss) - 1);
    tss_flush();          // ltr 0x28
}
```

`tss_flush()` 使用 `LTR` 指令加载 TSS 选择子：

```asm
tss_flush:
    movw $0x28, %ax
    ltr %ax
    ret
```

### 6.4 设置内核栈 (`set_user_kernel_stack`)

```cpp
void set_user_kernel_stack(uint64_t rsp0) {
    tss.rsp[0] = rsp0;
}
```

每个用户任务的内核栈通过分配物理页并设置 RSP0 来准备。

### 6.5 进入 Ring 3 (`enter_usermode`)

```asm
enter_usermode(entry, user_rsp)  ; RDI = entry, RSI = user_rsp
    cli
    movw $0x23, %ax      ; 用户数据段选择子 (RPL=3)
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    pushq $0x23           ; SS  = 用户数据段
    pushq %rsi            ; RSP = 用户栈顶
    pushq $0x202          ; RFLAGS = IF=1
    pushq $0x1B           ; CS  = 用户代码段 (RPL=3)
    pushq %rdi            ; RIP = 入口
    iretq                 ; 弹出 → 进入 ring 3 执行
```

RFLAGS 的 `0x202` 设置 IF=1 (开中断) 和 Reserved bit 1。

### 6.6 ELF 程序加载 (`elf_load`)

```
elf_load(elf_data, size, &info)
```

1. 验证 ELF magic (`\x7FELF`)、64-bit (`e_ident[4]==2`)、x86_64 (`e_machine==0x3E`)
2. 遍历所有 PT_LOAD segment (`p_type==1`)：
   - 计算所需页范围 (`page_start = vaddr & ~0xFFF`, `page_end = (vaddr + memsz + 0xFFF) & ~0xFFF`)
   - 逐页分配物理页并映射到用户虚拟地址空间：`vmm_map_page(page, phys, PAGE_PRESENT | PAGE_USER | (writable ? PAGE_WRITABLE : 0))`
   - 从 ELF 数据复制 `filesz` 字节
   - 零填充 `memsz - filesz` (BSS)
3. 刷新 TLB

### 6.7 启动用户任务 (`start_user_task`)

```
start_user_task(&info):
1. 分配 4KB 用户栈 → info.user_stack_top = base + 0x1000
2. 分配 4KB 内核栈 → kstack_top
3. 设置 TSS.RSP0 = kstack_top
4. 在内核栈上构建 switch_to 帧:
       [info]
       [user_kernel_bootstrap]  ← ret 地址
       [0, 0, 0, 0, 0, 0]      ← callee-saved 寄存器
5. 创建 Task 结构体, PID 从 100 开始
6. 调用 task_add() 加入调度链表
```

`user_kernel_bootstrap` 是在 ring 0 执行的过渡函数：

```cpp
extern "C" void user_kernel_bootstrap() {
    set_user_kernel_stack(pmm_alloc_page() + 0x1000);  // 重新分配内核栈
    enter_usermode(g_user_info->entry, g_user_info->user_stack_top);
}
```

当调度器选中这个任务并 `switch_to` 时：
1. 从 `info` 读出用户入口和用户栈顶
2. 分配新的内核栈给 TSS RSP0 (防止多任务切换时栈冲突)
3. 调用 `enter_usermode` → `iretq` 进入 ring 3

### 6.8 段选择子汇总

| 名称 | 选择子 | 索引 | RPL | 描述 |
|------|--------|------|-----|------|
| Kernel Code | `0x08` | 1 | 0 | 内核 64 位代码段 |
| Kernel Data | `0x10` | 2 | 0 | 内核数据段 |
| User Code | `0x1B` | 3 | 3 | 用户 64 位代码段 |
| User Data | `0x23` | 4 | 3 | 用户数据段 |
| TSS | `0x28` | 5 | 0 | TSS (LTR) |

---

## 7. 任务生命周期

### 7.1 内核初始化到第一个任务

```
kernel_main()
  │
  ├─ task_init()           // 初始化全局变量
  ├─ task_create(idle_entry)  → Task PID=1 (IDLE)
  ├─ task_create(shell_entry) → Task PID=2 (SHELL)
  ├─ scheduler_init()
  │
  ├─ g_current_task = idle
  ├─ idle->state = RUNNING
  ├─ sti()                 // 开中断
  │
  └─ idle_entry()          // IDLE 任务开始
```

### 7.2 IDLE 任务

```cpp
static void idle_entry() {
    while (1) {
        __asm__ volatile("hlt");     // 省电
        scheduler_yield();           // 让出 CPU
    }
}
```

空闲时 hlt 休眠，然后在 PIT 中断唤醒后调用 `scheduler_yield()` 尝试切换到 shell 或其他就绪任务。

### 7.3 SHELL 任务

```cpp
static void shell_entry() {
    shell_run();
}
```

`shell_run()` 内部循环读输入、解析命令、打印输出。在执行命令过程中（如 `echo`, `meminfo` 等）可能调用 `scheduler_yield()` 让出 CPU。

### 7.4 用户任务创建 (当前注释掉)

```cpp
// 在 kernel_main 中:
// UserTaskInfo uinfo;
// elf_load(hello_binary, hello_size, &uinfo);
// start_user_task(&uinfo);
```

当前启动进入 Shell，不再加载用户程序。取消注释可恢复用户程序创建。

### 7.5 完整的任务切换流程

以 Shell 任务让出 CPU 切到 IDLE 为例：

```
Shell 任务运行中
  │
  ├─ shell 内调用 scheduler_yield()
  │    ├─ 遍历环形链表: Shell → IDLE (READY)
  │    ├─ Shell.state = READY, IDLE.state = RUNNING
  │    ├─ g_current_task = IDLE
  │    └─ switch_to(&shell->rsp, &idle->rsp)
  │         ├─ [push rbx, rbp, r12-r15]     ← 保存 Shell 上下文
  │         ├─ [mov (%rdi), %rsp]            ← 保存 Shell 的 RSP
  │         ├─ [mov (%rsi), %rsp]            ← 恢复 IDLE 的 RSP
  │         ├─ [pop r15-r12, rbp, rbx]       ← 恢复 IDLE 上下文
  │         └─ ret → 回到 IDLE 的代码流
  │
IDLE 任务继续 (从 ret 后的指令)
  │
  ├─ hlt (等待中断)
  ├─ PIT 中断 → 唤醒, 执行 scheduler_yield()
  │    ├─ 遍历: IDLE → Shell (READY) ... 或仍在 IDLE
  │    └─ 如果找到 Shell READY → switch_to(...)
  │
Shell 任务恢复
```

---

## 8. 中断与调度协作

### 8.1 中断处理链

```
硬件中断 → CPU → IDT
                  │
                  └→ isr_stubs[i] → cli → push error/int_no → jmp isr_common
                                                                  │
                                                              isr_handler()
                                                                  │
                              ┌──────────────────────────────────┘
                              │
                    int_no == 32 → timer → scheduler_tick() (no-op)
                    int_no == 33 → kb_irq_handler()
                    int_no == 36 → serial_rx_irq_handler()
                    int_no == 128 → syscall_handler()
                              │
                    ┌─────────┘
                    ↓
              发送 EOI → iretq
```

### 8.2 当前局限性

- **协作式**：没有内核态任务抢占，一个任务如果不主动 yield 会一直占用 CPU
- **无单独内核栈**：所有用户任务共享 TSS RSP0（`start_user_task` 每次创建任务时分配新内核栈并设置，但多任务切换时可能覆盖）
- **无进程隔离**：所有任务共享同一份页表（identity map），用户程序可以访问内核数据
- **无 `fork`**：只有 `task_create` 静态创建新任务

---

## 9. 关键全局变量

```cpp
// task.cpp 中定义
volatile bool        g_need_resched   = false;   // 预留: 抢占标志
struct Task* volatile g_current_task  = nullptr;  // 当前运行的任务

// 用户模式
static UserTaskInfo* g_user_info;                // 当前用户任务信息
static uint64_t      g_next_user_pid = 100;      // 用户任务 PID 计数器
```

---

## 10. 数据流总结

```
创建: kernel_main → task_create(entry)
                        ↓
                   分配栈 + Task 结构体
                        ↓
                   构建 switch_to 帧
                        ↓
                   task_add() 到环形链表
                        ↓
                   调度器选中 → switch_to → 开始执行

运行: task_entry_wrapper → entry()
                              ↓
                         工作循环
                              │
                    ┌─────────┴──────────┐
                    ↓                     ↓
           scheduler_yield()      int 0x80 系统调用
                    │                     │
                    ↓                     ↓
              switch_to()           syscall_handler()
                    │                     │
                    ↓                     ↓
              新任务继续            yield / write / read / ...
```

---

## 11. 文件一览

| 文件 | 功能 |
|------|------|
| `task.hpp` | Task 结构体、状态枚举、函数声明 |
| `task.cpp` | 任务创建/初始化/退出、环形链表管理 |
| `scheduler.hpp` | 调度器函数声明 |
| `scheduler.cpp` | 协作式 Round-Robin 调度逻辑 |
| `switch.S` | `switch_to` 上下文切换汇编 |
| `syscall.hpp` | 系统调用初始化声明 |
| `syscall.cpp` | `int 0x80` 入口、系统调用表、各系统调用实现 |
| `user.hpp` | ELF 结构体、`UserTaskInfo`、用户模式函数声明 |
| `user.cpp` | TSS 初始化、ELF 加载、用户任务启动 |
| `user.S` | `enter_usermode`、`tss_flush` 汇编 |
| `isr_wrapper.S` | ISR 入口宏、`isr_common` 框架 |
| `isr.cpp` | `isr_handler` 中断分派、异常处理/panic |
| `isr.hpp` | `InterruptFrame` 结构体 |
| `gdt.cpp` | GDT 初始化、TSS 描述符安装 |
| `kernel.cpp` | 初始化入口，创建 IDLE + Shell 任务 |
| `user/hello.S` | 示例用户程序 |
