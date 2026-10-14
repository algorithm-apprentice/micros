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
        BYTE(0)
        *(.rodata .rodata.*)
        . = ALIGN(0x1000);
    } :rodata
    __micros_user_rodata_end = .;

    __micros_user_data_start = .;
    .data :
    {
        *(.data .data.*)
        . = ALIGN(MAX(16, ALIGNOF(.bss)));
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
    __micros_application_stack_top = 0x0000000080000000;

    /DISCARD/ :
    {
        *(.riscv.attributes)
        *(.comment)
        *(.note.GNU-stack)
    }
}

ASSERT(
    __micros_user_image_start == 0x0000000040000000,
    "application image start changed"
)
ASSERT(
    __micros_user_image_end <= 0x000000004000f000,
    "application image exceeds fifteen pages"
)
