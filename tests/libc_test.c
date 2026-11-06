#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d\n", __LINE__); ++failures; } } while (0)

int main(void)
{
    unsigned char *p;
    char text[64];
    char *end;
    FILE *file;
    int i;

    CHECK(strlen("hello") == 5 && strcmp("a", "b") < 0);
    CHECK(strncmp("abc", "abd", 2) == 0);
    strcpy(text, "abcdef");
    memmove(text + 1, text, 5);
    CHECK(strcmp(text, "aabcde") == 0);
    CHECK(strstr("abcde", "cd") != 0 && strchr("abc", 'b') != 0);
    strcpy(text, "aba");
    CHECK(strrchr(text, 'a') == text + 2);
    CHECK(strtol(" -0x20z", &end, 0) == -32 && *end == 'z');
    errno = 0;
    CHECK(strtol("2147483648", &end, 10) == 2147483647 && errno == ERANGE);
    errno = 0;
    CHECK(calloc((size_t)-1, 2) == 0 && errno == ENOMEM);
    errno = 0;
    CHECK(malloc(16U * 1024U * 1024U) == 0 && errno == ENOMEM);

    p = calloc(9000, 1);
    CHECK(p != 0);
    if (p)
    {
        for (i = 0; i < 9000; ++i) if (p[i]) { CHECK(0); break; }
        memset(p, 0x5a, 9000);
        p = realloc(p, 15000);
        CHECK(p != 0);
        if (p)
        {
            unsigned char *failed;
            CHECK(p[0] == 0x5a && p[8999] == 0x5a);
            failed = realloc(p, 16U * 1024U * 1024U);
            CHECK(failed == 0 && p[0] == 0x5a && p[8999] == 0x5a);
            free(p);
        }
    }
    p = malloc(8000);
    CHECK(p != 0);
    free(p);

    file = fopen("B:/WRITE.TXT", "w+");
    CHECK(file != 0);
    if (file)
    {
        CHECK(fwrite("abc", 1, 3, file) == 3);
        CHECK(ftell(file) == 3);
        CHECK(fseek(file, 0, SEEK_SET) == 0);
        CHECK(fread(text, 1, 3, file) == 3 && memcmp(text, "abc", 3) == 0);
        CHECK(fseek(file, -1, SEEK_END) == 0 && ftell(file) == 2);
        CHECK(fseek(file, 4, SEEK_SET) == 0 && ftell(file) == 4);
        CHECK(fseek(file, -5, SEEK_CUR) != 0);
        CHECK(fclose(file) == 0);
    }
    file = fopen("B:/WRITE.TXT", "a");
    CHECK(file != 0);
    if (file)
    {
        CHECK(fseek(file, 0, SEEK_SET) == 0);
        CHECK(fputs("def", file) == 0);
        CHECK(fclose(file) == 0);
    }
    file = fopen("B:/WRITE.TXT", "r");
    CHECK(file != 0);
    if (file)
    {
        memset(text, 0, sizeof(text));
        CHECK(fread(text, 1, 6, file) == 6);
        CHECK(strcmp(text, "abcdef") == 0);
        CHECK(fgetc(file) == EOF && feof(file));
        rewind(file);
        CHECK(!feof(file));
        CHECK(fputc('x', file) == EOF && ferror(file) && errno == EBADF);
        clearerr(file);
        CHECK(!ferror(file));
        CHECK(fclose(file) == 0);
    }
    errno = 0;
    CHECK(fopen("B:/NOFILE.TXT", "r") == 0 && errno != 0);
    CHECK(fopen("A:/Programs/HELLO.EXE", "r+") == 0);
    CHECK(fopen("A:/Programs/HELLO.EXE", "a") == 0);
    printf("LIBC TEST: %d failures\n", failures);
    return failures != 0;
}
