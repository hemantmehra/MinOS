#ifndef MINOS_ELF_UNISTD_H
#define MINOS_ELF_UNISTD_H
#include <stddef.h>
int chdir(const char *path);
char *getcwd(char *buffer, size_t size);
#endif
