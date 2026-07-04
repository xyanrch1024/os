# 内存管理与分页机制

## 1. 物理内存管理 (PMM)

### 1.1 整体架构

PMM 使用 **bitmap（位图）** 来追踪每个 4KB 物理页的使用状态：

- 每个 bit 对应一个 4KB 物理页（1 = 空闲，0 = 已分配）
- 128 MB 物理内存 → 32768 个页 → 4096 字节 bitmap

### 1.2 初始化 (`pmm_init`)

```
pmm_init(128) 的调用链：
kernel_main → pmm_init(128MB)
```

1. 计算总页数：`total_pages = 128MB / 4KB = 32768`
2. bitmap 大小：`total_pages / 8 = 4096 字节`
3. bitmap 位置：紧接在 `_kernel_end` 之后，4KB 对齐
4. 初始化 bitmap：全部标记为 1（空闲）
5. 标记已用页：从 page 0 到 bitmap 结束的所有页标记为 0（已分配）
   - 这包括：内核代码(.text/.rodata/.data/.bss)、boot 页表、boot 栈、PMM bitmap 自身

### 1.3 关键函数

| 函数 | 功能 | 实现 |
|------|------|------|
| `pmm_alloc_page()` | 分配一个 4KB 物理页 | 线性扫描 bitmap，找到第一个空闲 bit，清零后返回地址 |
| `pmm_free_page(addr)` | 释放一个 4KB 物理页 | 计算页号，对应 bit 置 1 |
| `pmm_free_pages()` | 查询空闲页数 | 返回 `free_pages` 计数器 |
| `pmm_total_pages()` | 查询总页数 | 返回 `total_pages` |

### 1.4 特点

- **简单**：顺序扫描，无缓存/ slab
- **无碎片**：任意页都可以分配，不需要连续
- **无多级**：bitmap 是平面的，不分 zone/NUMA
- 分配的页自动清零（写在 `pmm_alloc_page` 的 memset 循环中）

---

## 2. 虚拟内存管理 (VMM)

### 2.1 x86_64 四级页表

x86_64 使用 **4 级页表** 将 48 位虚拟地址转换为物理地址：

```
虚拟地址: [47:39] [38:30] [29:21] [20:12] [11:0]
             ↓        ↓       ↓       ↓     ↓
           PML4     PDPT      PD      PT    offset
           512      512      512     512   4KB内
```

- **PML4** (Page Map Level 4)：顶层页表，1 个表 512 项
- **PDPT** (Page Directory Pointer Table)：二级，每表 512 项
- **PD** (Page Directory)：三级，每表 512 项
- **PT** (Page Table)：四级，每表 512 项，每项映射 4KB

2MB 大页（`PAGE_HUGE`）跳过 PT 级：`PD` 项直接映射 2MB 连续区域。

### 2.2 CR3 寄存器

`CR3` 寄存器保存 PML4 的物理地址。`get_cr3()` 读取 CR3 的低 52 位（忽略标志位）：

```cpp
static uint64_t* get_cr3() {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return reinterpret_cast<uint64_t*>(cr3 & ~0xFFFULL);
}
```

### 2.3 初始化 (`vmm_init`)

```
kernel_main → vmm_init()
```

1. 读取 CR3 → PML4 地址
2. 在 PML4 所有已存在项上设置 `PAGE_USER` 标志（允许 ring 3 访问）
3. 在 PDPT 所有已存在项上设置 `PAGE_USER` 标志
4. 设置 PD 的 64 项（entry 0-63），每项映射 2MB：
   - 公式：`(i << 21) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_HUGE`
   - 结果：identity map 0 ~ 128MB
5. 刷新 TLB

注意：entry 0-3 原本由 bootloader 设置，这里被覆盖。但覆盖后的物理地址相同（`i << 21` 等于 bootloader 的地址计算），只是添加了 `PAGE_USER` 标志。

### 2.4 页表遍历 (`walk_page_table`)

核心辅助函数，本质上是手动执行四级页表查找：

```cpp
walk_page_table(virt, create)
```

- 参数 `create=true` 时，遇到不存在的项会分配新页表页
- 参数 `create=false` 时，仅查询已有映射

**大页拆分逻辑**：如果 PD 项标记为 `PAGE_HUGE`，需要拆分为 4KB 页：
1. 分配一个新的页表页（PT）
2. 将原来的 2MB 区域拆分为 512 个 4KB PTE
3. 每个 PTE 指向对应的物理地址（`base + i * 4096`）
4. 标记为 `PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER`
5. 将 PD 项改为指向这个新的 PT 页

### 2.5 映射函数 (`vmm_map_page`)

```cpp
vmm_map_page(0x401000, phys_page, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE)
```

1. 调用 `walk_page_table(virt, true)` → 获取或创建 PTE 指针
2. 设置 PTE = `phys | flags | PAGE_PRESENT`
3. 调用 `invlpg(virt)` 刷新 TLB

### 2.6 其他函数

| 函数 | 说明 |
|------|------|
| `vmm_unmap_page(virt)` | 清除 PTE，TLB 刷新 |
| `vmm_is_mapped(virt)` | 检查虚拟地址是否已映射 |
| `vmm_tlb_flush()` | 完全刷新 TLB（重新加载 CR3） |
| `vmm_invlpg(virt)` | 单地址 TLB 刷新 |

### 2.7 TLB 刷新策略

- 单页映射修改 → `invlpg`（单个地址刷新，高效）
- 批量修改 → `vmm_tlb_flush()`（完全刷新）

### 2.8 页表布局 (bootloader 设置)

bootloader 在进入长模式前创建初始页表：

| 表 | 地址 | 说明 |
|----|------|------|
| PML4 | ~0x106000 | 只有 entry 0 |
| PDPT | ~0x107000 | 只有 entry 0，指向 PD |
| PD | ~0x108000 | 4 个 2MB 大页 (0~8MB) |

boot 栈位于 `~0x109000~0x10D000` (16KB)。

### 2.9 标识映射总览

```
虚拟地址         物理地址         大小    页类型
0x000000-0x1FFFFF  0x000000-0x1FFFFF  2MB    2MB 大页 (boot)
0x200000-0x3FFFFF  0x200000-0x3FFFFF  2MB    2MB 大页 (kernel)
0x400000-0x5FFFFF  0x400000-0x5FFFFF  2MB    2MB 大页 (用户 mmap)
...                ...                ...    2MB 大页
0x7E00000-0x7FFFFFF  放大到 128MB
```

---

## 3. 内核堆分配器 (kmalloc)

### 3.1 整体架构

kmalloc 是基于 PMM 的 **first-fit** 堆分配器：

```
kmalloc → PMM (获取物理页) → 拆分 Block → 返回用户指针
kfree   → 合并相邻空闲 Block → (不归还给 PMM)
```

### 3.2 Block 结构

每个分配单元以 Block header 开头：

```
[ptr] → ┌──────────────┐
         │ Block header  │  ← HEADER_SIZE = sizeof(Block)
         ├──────────────┤
         │ 用户数据区    │  ← 返回的用户指针指向这里
         │ size 字节     │
         └──────────────┘
```

```cpp
struct Block {
    size_t    size;    // 数据区大小（不含 header）
    Block*    next;    // 链表下一块
    bool      free;    // 是否空闲
    uint64_t  magic;   // MAGIC_FREE 或 MAGIC_USED
};
```

magic 值用于检测损坏：
- 空闲块：`0xDEADBEEFCAFEBABE`
- 在用块：`0xCAFEBABEDEADBEEF`

### 3.3 初始化 (`kmalloc_init`)

1. 调用 `pmm_alloc_page()` 获取一个 4KB 物理页
2. 将整页作为一个空闲 Block：`size = 4096 - HEADER_SIZE`
3. 加入空闲链表

### 3.4 分配 (`kmalloc`)

```
kmalloc(size)：
1. 大小对齐到 8 字节
2. 扫描空闲链表，找第一个 size ≥ 请求的 Block
3. 如果 Block 足够大（余量 ≥ MIN_BLOCK_SIZE+16），拆分：
   原 Block → size = 请求大小，标记为已用
   新 Block → size = 剩余大小，标记为空闲，插入链表
4. 返回用户数据区指针（Block + HEADER_SIZE）
5. 如果找不到，分配新页 → 重复
```

### 3.5 释放 (`kfree`)

```
kfree(ptr)：
1. 从用户指针回退 HEADER_SIZE 得到 Block
2. 验证 magic 是否为 MAGIC_USED
3. 标记为空闲
4. 前向合并：如果 next 块也是空闲且地址连续，合并
5. 后向合并：如果 prev 块也是空闲且地址连续，合并
```

注意：Block 按地址排序（升序），通过比较指针地址插入。

### 3.6 重新分配 (`krealloc`)

分配新块 → 复制旧数据 → 释放旧块。简单实现，无原地扩容。

### 3.7 链表管理

Block 链表按地址升序排列：

```
heap_head → [Block A] → [Block B] → [Block C] → ...
            低地址                  高地址
```

新分配的页通过比较地址插入正确位置（保持升序），同时尝试与前面的空闲 Block 合并。

---

## 4. 启动时的内存布局

```
地址范围          内容                    所有者
────────          ────                    ──────
0x000000-0x100000  实模式/BIOS 区域        保留
0x100000-0x106000  多 32-bit 代码等        保留
0x106000-0x107000  PML4 页表              bootloader
0x107000-0x108000  PDPT 页表              bootloader
0x108000-0x109000  PD 页表               bootloader
0x109000-0x10D000  boot 栈 (16KB)        bootloader
0x200000-0x248xxx  内核 ELF (代码+数据+BSS) kernel
0x248xxx            内核结束 (_kernel_end) 
0x249xxx-0x24axxx  PMM bitmap            kernel/PMM
...                 空闲内存                PMM 管理
```

## 5. 两阶段构建的内存布局

```
第一阶段 (32-bit wrapper):
  boot.S → 32-bit 链接 → build/kernel.elf
  包含：multiboot header, 页表, 启动代码, kernel64.bin (作为 .data)
  
第二阶段 (64-bit kernel):
  kernel/*.cpp → 64-bit 链接 → build/kernel64.elf
  → objcopy → build/kernel64.bin (纯二进制)
  
  链接地址: 0x200000 (linker-kernel.ld 第 7 行: . = 2M)
  跳转: bootloader 复制 kernel64.bin 到 0x200000 → jmp 0x200000
```

## 6. 三层内存架构关系图

```
┌──────────────────────────────────────────┐
│           用户程序 (User Task)            │
│   malloc/free → syscall brk (未来)        │
└────────────────────┬─────────────────────┘
                     │
┌────────────────────▼─────────────────────┐
│           kmalloc 堆分配器                │
│   first-fit, Block 链表, magic 验证       │
└────────────────────┬─────────────────────┘
                     │ 申请/释放 4KB 页
┌────────────────────▼─────────────────────┐
│      VMM 虚拟内存管理 (x86_64 分页)        │
│   4 级页表: PML4 → PDPT → PD → PT        │
│   2MB 大页 / 4KB 小页                     │
└────────────────────┬─────────────────────┘
                     │ 分配/释放 物理页
┌────────────────────▼─────────────────────┐
│      PMM 物理内存管理 (Bitmap)             │
│   128MB / 4KB = 32768 pages → 4096 bytes  │
└──────────────────────────────────────────┘
```
