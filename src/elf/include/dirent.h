#ifndef MINOS_ELF_DIRENT_H
#define MINOS_ELF_DIRENT_H

typedef struct minos_directory DIR;
struct dirent { char d_name[256]; };

DIR *opendir(const char *path);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);

#endif
