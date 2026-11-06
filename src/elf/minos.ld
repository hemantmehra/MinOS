OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    image PT_LOAD FILEHDR PHDRS FLAGS(7);
}

SECTIONS
{
    . = 0x40000000 + SIZEOF_HEADERS;
    .text : { *(.text.start) *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    .data : { *(.data .data.*) } :image
    .bss : { *(.bss .bss.*) *(COMMON) } :image
    ASSERT(. <= 0x40010000, "ELF image exceeds the 64 KiB MinOS limit")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
