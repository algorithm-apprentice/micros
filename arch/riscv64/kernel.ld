OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x80200000;
    __kernel_start = .;

    .text : ALIGN(16)
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(16)
    {
        *(.data .data.*)
    }

    .bss (NOLOAD) : ALIGN(16)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }

    .stack (NOLOAD) : ALIGN(16)
    {
        KEEP(*(.stack))
    }

    .trap_stacks (NOLOAD) : ALIGN(16)
    {
        KEEP(*(.trap_stack))
        KEEP(*(.trap_emergency_stack))
    }

    __kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame .eh_frame.*)
        *(.note .note.*)
    }
}
