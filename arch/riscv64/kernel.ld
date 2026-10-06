OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x80200000;
    . = ALIGN(4096);
    __kernel_start = .;
    __kernel_text_start = .;

    .text : ALIGN(16)
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    . = ALIGN(4096);
    __kernel_text_end = .;
    __kernel_rodata_start = .;

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4096);
    __kernel_rodata_end = .;
    __kernel_writable_start = .;

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

    .thread_kernel_stacks (NOLOAD) : ALIGN(4096)
    {
        __thread_kernel_stacks_start = .;
        KEEP(*(.thread_kernel_stacks))
        __thread_kernel_stacks_end = .;
    }

    .trap_stacks (NOLOAD) : ALIGN(16)
    {
        KEEP(*(.trap_stack))
        KEEP(*(.trap_emergency_stack))
    }

    . = ALIGN(4096);
    __kernel_writable_end = .;
    __kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame .eh_frame.*)
        *(.note .note.*)
    }
}
