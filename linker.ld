OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS {
    text PT_LOAD FILEHDR PHDRS;
    note PT_NOTE;
}

SECTIONS {
    . = 1M;

    .note.pvh ALIGN(4) : {
        KEEP(*(.note.pvh))
    } :note

    .multiboot ALIGN(8) : {
        KEEP(*(.multiboot))
    } :text

    .text ALIGN(4K) : {
        KEEP(*(.text))
    } :text

    .rodata ALIGN(4K) : {
        *(.rodata)
    } :text

    .data ALIGN(4K) : {
        *(.data)
    } :text

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss)
    } :text

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }
}
