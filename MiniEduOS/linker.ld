ENTRY(_start)

SECTIONS
{
    /* The kernel starts loading at 1MB */
    . = 1M;

    .text : ALIGN(4K)
    {
        /* Ensure the Multiboot header is placed at the very beginning of the binary */
        *(.multiboot)
        *(.text*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }

    .data : ALIGN(4K)       
    {
        *(.data*)
    }

    .bss : ALIGN(4K) 
    {
        *(COMMON)
        *(.bss*)
    }
}