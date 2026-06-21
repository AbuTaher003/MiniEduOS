CC = x86_64-elf-gcc
AS = nasm
GRUB_MKRESCUE = i686-elf-grub-mkrescue
GRUB_FILE = i686-elf-grub-file

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -mno-sse -mno-sse2
LDFLAGS = -m32 -Wl,-m,elf_i386 -T linker.ld -nostdlib -ffreestanding

.PHONY: all clean run

all: os.iso

boot.o: boot.s
	$(AS) -f elf32 boot.s -o boot.o

kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) -c kernel/kernel.c -o kernel.o

kernel.elf: boot.o kernel.o linker.ld
	$(CC) $(LDFLAGS) boot.o kernel.o -o kernel.elf
	$(GRUB_FILE) --is-x86-multiboot kernel.elf

os.iso: kernel.elf boot/grub.cfg
	mkdir -p iso/boot/grub
	cp kernel.elf iso/boot/kernel.elf
	cp boot/grub.cfg iso/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o os.iso iso

run: os.iso
	qemu-system-x86_64 -cdrom os.iso

clean:
	rm -f boot.o kernel.o kernel.elf os.iso
	rm -rf iso/boot/kernel.elf iso/boot/grub/grub.cfg

