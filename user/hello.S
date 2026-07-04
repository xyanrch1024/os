.section .text
.globl _start
_start:
    movq $2, %rax
    int $0x80

    movq $1, %rax
    movq $1, %rdi
    leaq msg(%rip), %rsi
    movq $len, %rdx
    int $0x80

    movq $0, %rax
    int $0x80

    jmp _start

.section .rodata
msg:
    .ascii "Hello from userspace!\n"
len = . - msg
