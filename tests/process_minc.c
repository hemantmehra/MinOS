#include <stdio.h>
#include <string.h>

int main(int argc, char **argv, char **envp) {
    if (argc != 2 || strcmp(argv[0], "A:/Programs/MINTST.EXE") != 0 ||
        strcmp(argv[1], "arg") != 0 || argv[2] != 0 ||
        envp[0] == 0 || strcmp(envp[0], "PATH=A:/Programs") != 0 ||
        envp[1] == 0 || strcmp(envp[1], "TEST=hello") != 0 ||
        envp[2] != 0) {
        return 98;
    }
    return 77;
}
