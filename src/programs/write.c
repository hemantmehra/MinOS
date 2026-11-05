#include <stdio.h>
#include <minos.h>

int main() {
    int arguments[128];
    int contents[256];

    if (read_arguments(arguments) < 0) {
        printf("Usage: write B:/file <text>\n");
        return 0;
    }

    int index = 0;
    while (arguments[index] != 0 && arguments[index] != ' ') {
        index++;
    }
    if (arguments[index] == 0) {
        printf("Usage: write B:/file <text>\n");
        return 0;
    }
    arguments[index] = 0;
    index++;
    while (arguments[index] == ' ') {
        index++;
    }

    int length = 0;
    while (arguments[index] != 0 && length < 256) {
        contents[length] = arguments[index];
        length++;
        index++;
    }
    if (arguments[index] != 0) {
        printf("write: text is limited to 256 bytes.\n");
        return 0;
    }

    int handle = open_file(arguments);
    if (handle < 0) {
        printf("write: could not open file. Use an existing B: file.\n");
        return 0;
    }
    if (truncate_file(handle) < 0) {
        close_file(handle);
        printf("write: writing is supported only on B: files.\n");
        return 0;
    }
    int result = write_file(handle, contents, length);
    close_file(handle);

    if (result < 0) {
        printf("write: disk write failed or file capacity was exceeded.\n");
    } else {
        printf("Wrote %d bytes.\n", result);
    }
    return 0;
}
