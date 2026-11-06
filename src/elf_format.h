#ifndef MINOS_ELF_FORMAT_H
#define MINOS_ELF_FORMAT_H

#define ELF_IMAGE_BASE 0x40000000U
#define ELF_IMAGE_LIMIT (4U * 1024U * 1024U)
#define ELF_HEADER_SIZE 52
#define ELF_PROGRAM_HEADER_SIZE 32
#define ELF_MAX_PROGRAM_HEADERS 32
#define ELF_LOAD 1
#define ELF_DYNAMIC 2
#define ELF_INTERP 3
#define ELF_TLS 7
#define ELF_EXECUTABLE 1

int elf_validate(const unsigned char *data, unsigned int size,
    unsigned int *entry_offset, unsigned int *image_size);

#endif
