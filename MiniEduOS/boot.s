; Multiboot 1 Header constants
MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000007  ; align loaded modules on page boundaries, request memory info, request graphics mode
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
    ; Address fields (dummy values since we are ELF and bit 16 is not set)
    dd 0 ; header_addr
    dd 0 ; load_addr
    dd 0 ; load_end_addr
    dd 0 ; bss_end_addr
    dd 0 ; entry_addr
    ; Graphics mode fields
    dd 0 ; mode_type (0 = linear graphics, 1 = text)
    dd 800 ; width
    dd 600 ; height
    dd 32 ; depth

section .text
global _start
extern kernel_main

_start:
    ; Disable interrupts
    cli

    ; Initialize stack pointer
    mov esp, stack_top

    ; Push Multiboot parameters
    push ebx ; mb_addr
    push eax ; magic

    ; Call the C kernel
    call kernel_main

.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384 ; 16 KiB stack
stack_top: