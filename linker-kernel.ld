OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_kernel_entry)

SECTIONS {
    . = 2M;

    .text ALIGN(4K) : {
        KEEP(*(.text.kentry))
        KEEP(*(.text))
    }

    .rodata ALIGN(4K) : {
        *(.rodata)
    }

    .data ALIGN(4K) : {
        *(.data)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss)
    }

    _kernel_end = ALIGN(4K);

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }
}
