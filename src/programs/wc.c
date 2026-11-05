#include <stdio.h>
#include <minos.h>

int main() {
    int buffer[256];
    int in_word = 0;
    int words = 0;
    int finished = 0;

    while (!finished) {
        int count = read_stream(0, buffer);
        if (count < 0) {
            finished = 1;
        }
        for (int index = 0; index < count; index++) {
            int character = buffer[index];
            if (character == ' ' || character == 9 ||
                character == 10 || character == 13) {
                in_word = 0;
            } else if (!in_word) {
                words++;
                in_word = 1;
            }
        }
    }

    printf("%d\n", words);
    return 0;
}
