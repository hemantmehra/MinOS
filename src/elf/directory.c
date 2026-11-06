#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

struct minos_directory
{
    int handle;
    int active;
    struct dirent entry;
};

static DIR directories[8];

static int call1(int number, int argument)
{
    int result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number), "b"(argument)
                      : "memory", "cc");
    return result;
}

static int path_array(const char *path, int array[128])
{
    int i;
    if (!path) { errno = EINVAL; return 0; }
    if (!*path) { errno = ENOENT; return 0; }
    for (i = 0; i < 127 && path[i]; ++i) array[i] = (unsigned char)path[i];
    if (path[i]) { errno = EINVAL; return 0; }
    array[i] = 0;
    return 1;
}

DIR *opendir(const char *path)
{
    int array[128], handle, i;
    if (!path_array(path, array)) return 0;
    for (i = 0; i < 8 && directories[i].active; ++i) {}
    if (i == 8) { errno = EMFILE; return 0; }
    handle = call1(12, (int)array);
    if (handle < 0) { errno = EIO; return 0; }
    directories[i].handle = handle;
    directories[i].active = 1;
    return &directories[i];
}

struct dirent *readdir(DIR *directory)
{
    int buffer[256], result, i;
    if (!directory || !directory->active) { errno = EBADF; return 0; }
    __asm__ volatile ("int $0x80" : "=a"(result)
        : "a"(14), "b"(directory->handle), "c"(buffer), "d"(256)
        : "memory", "cc");
    if (result < 0) { errno = EIO; return 0; }
    if (!result) return 0;
    for (i = 0; i < 256; ++i)
    {
        directory->entry.d_name[i] = (char)buffer[i];
        if (!buffer[i]) break;
    }
    return &directory->entry;
}

int closedir(DIR *directory)
{
    if (!directory || !directory->active) { errno = EBADF; return -1; }
    if (call1(13, directory->handle) < 0) { errno = EIO; return -1; }
    directory->active = 0;
    return 0;
}

int mkdir(const char *path, mode_t mode)
{
    int array[128], result;
    (void)mode;
    if (!path_array(path, array)) return -1;
    result = call1(43, (int)array);
    if (result != 0)
    {
        if (result == 1) errno = EINVAL;
        else if (result == 2) errno = EEXIST;
        else if (result == 3 || result == 4) errno = ENOSPC;
        else errno = EIO;
        return -1;
    }
    return 0;
}

int chdir(const char *path)
{
    int array[128];
    if (!path_array(path, array)) return -1;
    if (call1(44, (int)array) < 0) { errno = EIO; return -1; }
    return 0;
}

char *getcwd(char *buffer, size_t size)
{
    int array[128], result, i;
    if (!buffer || size == 0) { errno = EINVAL; return 0; }
    __asm__ volatile ("int $0x80" : "=a"(result)
        : "a"(16), "c"(array), "d"(128) : "memory", "cc");
    if (result < 0) { errno = EIO; return 0; }
    if ((size_t)result >= size) { errno = ERANGE; return 0; }
    for (i = 0; i <= result; ++i) buffer[i] = (char)array[i];
    return buffer;
}
