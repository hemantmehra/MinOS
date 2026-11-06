#include <stdio.h>

int main(void)
{
    unsigned int count = 0;
    while (getchar() != EOF) ++count;
    if (ferror(stdin) || !feof(stdin))
    {
        puts("PIPE TEST: error");
        return 1;
    }
    printf("PIPE TEST: %u bytes\n", count);
    return 0;
}
