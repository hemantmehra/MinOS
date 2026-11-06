#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d\n", __LINE__); ++failures; } } while (0)

int main(int argc, char **argv)
{
    FILE *file;
    DIR *dir;
    struct dirent *entry;
    char text[128];
    int found = 0, i;

    if (argc > 1)
    {
        file = fopen("B:/CLIB/NEW.TXT", "r");
        CHECK(file != 0);
        if (file)
        {
            CHECK(fread(text, 1, 3, file) == 3 && memcmp(text, "aXc", 3) == 0);
            CHECK(fclose(file) == 0);
        }
        file = fopen("B:/CLIB/F089.TXT", "r");
        CHECK(file != 0);
        if (file) CHECK(fclose(file) == 0);
        printf("FS REBOOT: %d failures\n", failures);
        return failures != 0;
    }
    CHECK(mkdir("B:/CLIB", 0755) == 0);
    CHECK(mkdir("B:/CLIB", 0755) == -1 && errno == EEXIST);
    CHECK(chdir("") == -1 && errno == ENOENT);
    CHECK(chdir("B:/CLIB") == 0);
    CHECK(getcwd(text, sizeof(text)) && strcmp(text, "B:/CLIB") == 0);
    CHECK(getcwd(text, 2) == 0 && errno == ERANGE);
    file = fopen("NEW.TXT", "w+");
    CHECK(file != 0);
    if (file)
    {
        CHECK(fputs("abc", file) == 0);
        CHECK(fseek(file, 1, SEEK_SET) == 0);
        CHECK(fputc('X', file) == 'X');
        CHECK(fseek(file, 0, SEEK_SET) == 0);
        CHECK(fread(text, 1, 3, file) == 3 && memcmp(text, "aXc", 3) == 0);
        CHECK(fseek(file, 1026, SEEK_SET) == 0);
        CHECK(fputc('Z', file) == 'Z');
        CHECK(fseek(file, 3, SEEK_SET) == 0);
        for (i = 3; i < 1026; ++i) if (fgetc(file) != 0) { CHECK(0); break; }
        CHECK(fgetc(file) == 'Z');
        CHECK(fseek(file, -1028, SEEK_CUR) == -1);
        CHECK(fclose(file) == 0);
    }
    file = fopen("APP.TXT", "a");
    CHECK(file != 0);
    if (file) { CHECK(fputs("ok", file) == 0); CHECK(fclose(file) == 0); }
    file = fopen("APP.TXT", "a+");
    CHECK(file != 0);
    if (file)
    {
        CHECK(fseek(file, 0, SEEK_SET) == 0);
        CHECK(fputs("!", file) == 0);
        CHECK(fseek(file, 0, SEEK_SET) == 0);
        CHECK(fread(text, 1, 3, file) == 3 && memcmp(text, "ok!", 3) == 0);
        CHECK(fclose(file) == 0);
    }
    file = fopen("APP.TXT", "w+");
    CHECK(file != 0);
    if (file)
    {
        CHECK(fseek(file, 10, SEEK_SET) == 0 && fputc('z', file) == 'z');
        CHECK(fseek(file, 0, SEEK_SET) == 0);
        for (i = 0; i < 10; ++i) if (fgetc(file) != 0) { CHECK(0); break; }
        CHECK(fgetc(file) == 'z');
        CHECK(fclose(file) == 0);
    }
    for (i = 0; i < 90; ++i)
    {
        char name[] = "F000.TXT";
        name[2] = (char)('0' + i / 10);
        name[3] = (char)('0' + i % 10);
        file = fopen(name, "w");
        if (!file) { CHECK(0); break; }
        if (fclose(file) != 0) { CHECK(0); break; }
    }
    dir = opendir(".");
    CHECK(dir != 0);
    if (dir)
    {
        errno = 0;
        while ((entry = readdir(dir)) != 0)
            if (strcmp(entry->d_name, "NEW.TXT") == 0) found = 1;
        CHECK(errno == 0 && found);
        CHECK(closedir(dir) == 0);
    }
    CHECK(fopen("A:/NOFILE.TXT", "w") == 0);
    CHECK(mkdir("A:/NOTHERE", 0755) == -1);
    CHECK(fopen("B:/NO-DIR/FILE.TXT", "w") == 0);
    CHECK(chdir("A:/") == 0);
    printf("FS TEST: %d failures\n", failures);
    return failures != 0;
}
