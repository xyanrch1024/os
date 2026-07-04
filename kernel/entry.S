.code64

.section .bss
.align 16
.globl kernel_stack_top
kernel_stack_bottom:
    .skip 16384
kernel_stack_top:

.section .text.kentry, "ax"
.globl _kernel_entry
_kernel_entry:
    movq $kernel_stack_top, %rsp

    movl %edi, %edi
    movl %esi, %esi

    call kernel_main

    hlt
