#include <stdio.h>
#include <minos.h>

int main() {
    int number = parse_int(read_line("Enter n (0-12): ", 2), 0);

    if (number < 0 || number > 12) {
        printf("Input out of range; use 0-12.\n");
    } else {
        int result = 1;
        for (int factor = 2; factor <= number; factor++) {
            result *= factor;
        }
        printf("n! = %d = %d\n", number, result);
    }
    return 0;
}
