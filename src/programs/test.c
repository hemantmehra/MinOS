#include <stdio.h>

struct Point {
    int x;
    int y;
};

int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}

int main() {
    printf("Hello from C\n");
    printf("Number for today : %d\n", 42);
    int x = fact(5);
    if (x > 10) {
        printf("Factorial is greater than 10\n");
    } else {
        printf("Factorial is not greater than 10\n");
    }
    char character = 'A';
    switch (character) {
        case 'A':
            printf("Character is A\n");
            break;
        case 'B':
            printf("Character is B\n");
            break;
        default:
            printf("Character is neither A nor B\n");
    }
    printf("Factorial of 5 is %d\n", x);

    int arr[5] = {1, 2, 3, 4, 5};
    for (int i = 0; i < 5; i++) {
        printf("Array element %d: %d\n", i, arr[i]);
    }

    float f = 3.14;
    printf("Value of f: %.2f\n", f);

    struct Point p1 = {10, 20};
    printf("Point coordinates: (%d, %d)\n", p1.x, p1.y);

    char *a = "Hello";
    char *b = "World";
    printf("%s %s\n", &a[1], b+1);
    return 0;
}