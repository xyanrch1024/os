.code32

.set MULTIBOOT_MAGIC,    0x1BADB002
.set MULTIBOOT_FLAGS,    0x00000003
.set MULTIBOOT_CHECKSUM, -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

.section .multiboot, "a"
.align 4
multiboot_header:
    .long MULTIBOOT_MAGIC
    .long MULTIBOOT_FLAGS
    .long MULTIBOOT_CHECKSUM

.section .data
.align 8
.globl kernel_binary
kernel_binary:
    .incbin "build/kernel64.bin"
kernel_binary_end:
.globl kernel_binary_size
kernel_binary_size:
    .long kernel_binary_end - kernel_binary

.section .bss
.align 4096
pml4:
    .skip 4096
pdpt:
    .skip 4096
pd:
    .skip 4096

stack_bottom:
    .skip 16384
stack_top:

.section .text
.code32
.globl _start
_start:
    cli
    movl $stack_top, %esp

    pushl %eax
    pushl %edx
    movw $0x3FA, %dx
    movb $0x03, %al
    outb %al, %dx
    popl %edx
    popl %eax

    pushl $0
    pushl %eax
    pushl $0
    pushl %ebx

    cmpl $0x2BADB002, %eax
    jne .no_multiboot

    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $0x200000, %eax
    pushl %eax
    popfl
    pushfl
    popl %eax
    pushl %ecx
    popfl
    cmpl %ecx, %eax
    je .no_cpuid

    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb .no_longmode
    movl $0x80000001, %eax
    cpuid
    testl $0x20000000, %edx
    jz .no_longmode

    movl $pml4, %edi
    xorl %eax, %eax
    movl $((4096*3)/4), %ecx
    rep stosl

    movl $pdpt, %eax
    orl $3, %eax
    movl %eax, pml4
    movl $0, pml4+4

    movl $pd, %eax
    orl $3, %eax
    movl %eax, pdpt
    movl $0, pdpt+4

    movl $0x83, pd
    movl $0, pd+4
    movl $0x200083, pd+8
    movl $0, pd+12
    movl $0x400083, pd+16
    movl $0, pd+20
    movl $0x600083, pd+24
    movl $0, pd+28

    movl $pml4, %eax
    movl %eax, %cr3

    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0

    pushl kernel_binary_size
    pushl $kernel_binary

    lgdt gdt64_desc
    ljmp $0x08, $long_mode_start

.no_multiboot:
    movl $0x4F424F4D, 0xB8000
    hlt
    jmp .no_multiboot

.no_cpuid:
    movl $0x4F504F43, 0xB8000
    hlt
    jmp .no_cpuid

.no_longmode:
    movl $0x4F4D4F4C, 0xB8000
    hlt
    jmp .no_longmode

.section .rodata
.align 8
gdt64:
    .quad 0
gdt64_code:
    .word 0
    .word 0
    .byte 0
    .byte 0x9A
    .byte 0xA0
    .byte 0
gdt64_data:
    .word 0
    .word 0
    .byte 0
    .byte 0x92
    .byte 0
    .byte 0
gdt64_desc:
    .word gdt64_desc - gdt64 - 1
    .long gdt64
    .long 0

.section .text
.code64
long_mode_start:
    cli
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    movl 8(%rsp), %r12d
    movl 16(%rsp), %r13d

    movl (%rsp), %esi
    movl 4(%rsp), %ecx
    movl $0x200000, %edi
    addl %ecx, %esi
    decl %esi
    addl %ecx, %edi
    decl %edi
    std
    rep movsb
    cld

    movl %r12d, %edi
    movl %r13d, %esi
    addq $24, %rsp

    movl $0x200000, %eax
    call *%rax

    hlt
