#ifndef MINOS_FILESYSTEM_H
#define MINOS_FILESYSTEM_H

#include "kernel.h"

enum CreateDirectoryResult
{
    CREATE_DIRECTORY_OK,
    CREATE_DIRECTORY_INVALID_NAME,
    CREATE_DIRECTORY_EXISTS,
    CREATE_DIRECTORY_FULL,
    CREATE_DIRECTORY_DISK_FULL,
    CREATE_DIRECTORY_IO_ERROR,
    CREATE_DIRECTORY_FILESYSTEM_ERROR
};

enum FileReadResult
{
    FILE_READ_OK,
    FILE_READ_INVALID_NAME,
    FILE_READ_NOT_FOUND,
    FILE_READ_TOO_LARGE,
    FILE_READ_DISK_ERROR,
    FILE_READ_CORRUPT
};

int filesystem_open_directory(unsigned int owner, const char *path);
int filesystem_read_directory(
    unsigned int owner,
    unsigned int stream,
    unsigned int *name,
    unsigned int capacity);
int filesystem_close_directory(unsigned int owner, unsigned int stream);
void filesystem_close_process_directories(unsigned int owner);
int filesystem_open_file(unsigned int owner, const char *path);
int filesystem_read_file(
    unsigned int owner,
    unsigned int handle,
    unsigned int *buffer,
    unsigned int capacity);
int filesystem_write_file(
    unsigned int owner,
    unsigned int handle,
    const unsigned int *buffer,
    unsigned int length);
int filesystem_truncate_file(unsigned int owner, unsigned int handle);
int filesystem_seek_file(
    unsigned int owner, unsigned int handle, int offset, int whence);
int filesystem_close_file(unsigned int owner, unsigned int handle);
void filesystem_close_process_files(unsigned int owner);
int filesystem_change_directory(const char *path);
int filesystem_get_current_directory(char *path, unsigned int capacity);
enum FileReadResult filesystem_read_root_file(
    const char *name,
    unsigned char *buffer,
    unsigned int buffer_size,
    unsigned int *file_size);
enum CreateDirectoryResult filesystem_create_directory(const char *path);

#endif
