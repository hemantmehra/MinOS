#include <stdio.h>
#include <string.h>
#include <minos.h>

int main() {
    int values[100];
    int count = 0;
    char *command = read_line("> ", 31);

    while (strcmp(command, "exit") != 0) {
        if (starts_with(command, "push ")) {
            int value = parse_int(command, 5);
            if (value < 0) {
                printf("usage: push <number>\n");
            } else if (count >= 100) {
                printf("stack full\n");
            } else {
                values[count] = value;
                count++;
                printf("%d\n", value);
            }
        } else if (strcmp(command, "pop") == 0) {
            if (count == 0) {
                printf("null\n");
            } else {
                count--;
                printf("%d\n", values[count]);
            }
        } else if (strcmp(command, "top") == 0) {
            if (count == 0) {
                printf("null\n");
            } else {
                printf("%d\n", values[count - 1]);
            }
        } else {
            printf("unknown command\n");
        }

        command = read_line("> ", 31);
    }
    return 0;
}
