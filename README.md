# MyOS — x86_64 Kernel from Scratch

A minimal x86_64 hobby operating system kernel written in C++17, bootable on QEMU.

## Features

- **Boot**: Multiboot1 + 32-bit ELF wrapper → 64-bit long mode via GAS assembler
- **Memory**: PMM (bitmap allocator), VMM (4KB/2MB page tables), kmalloc (first-fit heap)
- **Multitasking**: Task struct with PID, cooperative round-robin scheduler, `switch_to` context switch
- **Syscalls**: `int 0x80` handler (yield/write/getpid/exit), accessible from ring 3
- **User Mode**: Ring 3 switching via TSS + iretq, ELF64 loader with VMM page mapping
- **Drivers**: PS/2 keyboard (IRQ1, scancode→ASCII), serial COM1 (IRQ4 RX/TX), VGA text mode
- **Shell**: Interactive command-line with `help`, `clear`, `echo`, `meminfo`, `ticks`, `mtest`, `reboot`
- **Timer**: PIT at 100 Hz, IRQ0-driven tick counter

## Build & Run

```bash
make run
```

Requires: `g++`, `gcc`, `ld`, `objcopy`, `qemu-system-x86_64`

## Project Structure

```
boot/        — 32-bit boot wrapper (boot.S, linker)
kernel/      — Core kernel (21 source files)
libc/        — Freestanding C++ stubs
user/        — User-mode programs (hello.S)
```

## Phases

| Phase | Milestone |
|-------|-----------|
| 1 | Bootloader, TTY, GDT, IDT, ISR, PIC, PIT, serial |
| 2 | PMM, VMM, kmalloc heap allocator |
| 3 | Keyboard driver, serial RX, shell |
| 4 | Task struct, context switch, scheduler, syscalls |
| 5 | TSS, ring 3 user mode, ELF loader |
