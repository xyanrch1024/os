.section .text
.code64

.global enter_usermode
enter_usermode:
    cli
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    pushq $0x23
    pushq %rsi
    pushq $0x202
    pushq $0x1B
    pushq %rdi
    iretq

.global tss_flush
tss_flush:
    movw $0x28, %ax
    ltr %ax
    ret
