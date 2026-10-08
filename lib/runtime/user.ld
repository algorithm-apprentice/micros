OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    writable PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x0000000040000000;
    __micros_user_image_start = .;
    __micros_user_text_start = .;

    .text :
    {
        KEEP(*(.text.start))
        *(.text .text.*)
        . = ALIGN(0x1000);
    } :text
    __micros_user_text_end = .;

    __micros_user_rodata_start = .;
    .rodata :
    {
        *(.rodata .rodata.*)
        . = ALIGN(0x1000);
    } :rodata
    __micros_user_rodata_end = .;

    __micros_user_data_start = .;
    .data :
    {
        *(.data .data.*)
    } :writable
    __micros_user_data_end = .;

    .bss (NOLOAD) :
    {
        __micros_user_bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(0x1000);
        __micros_user_bss_end = .;
    } :writable
    __micros_user_image_end = .;

    .riscv.attributes 0 :
    {
        *(.riscv.attributes)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note.GNU-stack)
    }
}

ASSERT(
    __micros_user_image_start == 0x0000000040000000,
    "user image start changed"
)
ASSERT(
    __micros_user_image_end <= 0x000000007ffff000,
    "user image overlaps the minimum external stack"
)
