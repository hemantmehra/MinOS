/* minedit: a small full-screen text editor.
   Usage: minedit <file>
   Ctrl+W writes the buffer to the file, Ctrl+X exits (press it twice to
   discard unsaved changes). Arrow keys, Home, End, Delete, Backspace and
   Enter edit the text. Only existing B: files can be written. */
#include <stdio.h>
#include <minos.h>

/* Screen layout: 36 rows of 144 columns; the last row is the status line. */

int line_start(int *text, int pos) {
    while (pos > 0 && text[pos - 1] != '\n') {
        pos--;
    }
    return pos;
}

int line_end(int *text, int length, int pos) {
    while (pos < length && text[pos] != '\n') {
        pos++;
    }
    return pos;
}

/* Screen row of pos when the text is drawn starting at top. */
int rows_between(int *text, int top, int pos) {
    int row = 0;
    int col = 0;
    for (int i = top; i < pos; i++) {
        if (text[i] == '\n') {
            row++;
            col = 0;
        } else {
            col++;
            if (col == 144) {
                col = 0;
                row++;
            }
        }
    }
    return row;
}

/* Scrolls by whole lines until the cursor is inside the 34 text rows. */
int adjust_top(int *text, int length, int top, int cursor) {
    if (cursor < top) {
        top = line_start(text, cursor);
    }
    while (rows_between(text, top, cursor) >= 34) {
        int end = line_end(text, length, top);
        if (end >= length) {
            break;
        }
        top = end + 1;
    }
    return top;
}

void print_name(char *name) {
    for (int i = 0; name[i] != 0 && i < 40; i++) {
        putchar(name[i]);
    }
}

/* Blank the rest of a row; the cursor wraps to the next row by itself. */
void pad_row(int col) {
    while (col < 144) {
        putchar(' ');
        col++;
    }
}

/* Blank the status row (all but the last cell, to avoid scrolling), then
   return to its start. */
void pad_status() {
    for (int k = 0; k < 143; k++) {
        putchar(' ');
    }
    putchar('\r');
}

void draw(int *text, int length, int cursor, int top, char *name,
          int status, int value, int dirty) {
    terminal_home();

    int row = 0;
    int col = 0;
    int i = top;
    while (i <= length && row < 34) {
        int ch = 0;
        if (i < length) {
            ch = text[i];
        }
        if (i == cursor) {
            /* Codes 128-254 draw character (code - 128) inverted; 255 is a
               solid block for the end of a line or file. */
            if (i < length && ch != '\n' && ch >= 32 && ch < 127) {
                putchar(ch + 128);
            } else {
                putchar(255);
            }
            col++;
        } else if (i < length && ch != '\n') {
            putchar(ch);
            col++;
        }
        if (col == 144) {
            col = 0;
            row++;
        }
        if (i < length && ch == '\n') {
            pad_row(col);
            row++;
            col = 0;
        }
        i++;
    }
    if (col > 0 && row < 34) {
        pad_row(col);
        row++;
    }
    while (row < 35) {
        pad_row(0);
        row++;
    }
    pad_status();
    int line = 1;
    for (int k = 0; k < cursor; k++) {
        if (text[k] == '\n') {
            line++;
        }
    }
    int column = cursor - line_start(text, cursor) + 1;

    if (status == 1) {
        printf("Wrote %d bytes to ", value);
        print_name(name);
    } else if (status == 2) {
        printf("Write failed: could not open the file (it must exist)");
    } else if (status == 3) {
        printf("Write failed: only B: files can be written");
    } else if (status == 4) {
        printf("Write failed: disk error or no free space");
    } else if (status == 5) {
        printf("Unsaved changes! Press Ctrl+X again to exit anyway");
    } else if (status == 6) {
        printf("Buffer full (1000 characters)");
    } else if (status == 7) {
        printf("Opened ");
        print_name(name);
        printf(" (%d bytes)", value);
    } else if (status == 8) {
        printf("New file ");
        print_name(name);
    }
    if (status != 0) {
        printf(" | ");
    }
    printf("Ln %d, Col %d", line, column);
    if (dirty) {
        printf(" [modified]");
    }
    printf(" | ^W Write  ^X Exit");
}

/* Writes the buffer to the file. Returns the byte count, or a negative
   status code: -2 cannot open, -3 not writable, -4 write failed. */
int save_file(int *text, int length, char *name, int *chunk) {
    int file = open_file(name);
    if (file < 0) {
        return -2;
    }
    if (truncate_file(file) < 0) {
        close_file(file);
        return -3;
    }
    int written = 0;
    while (written < length) {
        int count = length - written;
        if (count > 256) {
            count = 256;
        }
        for (int i = 0; i < count; i++) {
            chunk[i] = text[written + i];
        }
        int result = write_file(file, chunk, count);
        if (result <= 0) {
            close_file(file);
            return -4;
        }
        written += result;
    }
    close_file(file);
    return written;
}

int main(int argc, char *argv[]) {
    int text[1000];
    int chunk[256];

    if (argc < 2) {
        printf("Usage: minedit <file>\n");
        return 0;
    }
    char *name = argv[1];

    int length = 0;
    int status = 8;
    int value = 0;
    int file = open_file(name);
    if (file >= 0) {
        int count = read_file(file, chunk);
        while (count > 0) {
            for (int i = 0; i < count; i++) {
                if (chunk[i] == '\r') {
                    continue;
                }
                if (length >= 1000) {
                    close_file(file);
                    printf("minedit: file is larger than 1000 characters.\n");
                    return 0;
                }
                text[length] = chunk[i];
                length++;
            }
            count = read_file(file, chunk);
        }
        close_file(file);
        if (count < 0) {
            printf("minedit: could not read file.\n");
            return 0;
        }
        status = 7;
        value = length;
    }

    int cursor = 0;
    int top = 0;
    int dirty = 0;
    int shift = 0;
    int ctrl = 0;
    int caps = 0;
    int extended = 0;
    int redraw = 1;
    int quit = 0;

    while (!quit) {
        if (redraw) {
            top = adjust_top(text, length, top, cursor);
            draw(text, length, cursor, top, name, status, value, dirty);
            redraw = 0;
        }

        int code = read_key();
        if (code == 0) {
            continue;
        }
        if (code == 0xE0) {
            extended = 1;
            continue;
        }
        int was_extended = extended;
        extended = 0;

        if (code == 0x2A || code == 0x36) {
            shift = 1;
            continue;
        }
        if (code == 0xAA || code == 0xB6) {
            shift = 0;
            continue;
        }
        if (code == 0x1D) {
            ctrl = 1;
            continue;
        }
        if (code == 0x9D) {
            ctrl = 0;
            continue;
        }
        if (code >= 0x80) {
            continue;
        }
        if (code == 0x3A && !was_extended) {
            caps = 1 - caps;
            continue;
        }

        int previous = status;
        status = 0;
        redraw = 1;

        if (ctrl) {
            if (code == 0x11) {
                int result = save_file(text, length, name, chunk);
                if (result >= 0) {
                    status = 1;
                    value = result;
                    dirty = 0;
                } else {
                    status = 0 - result;
                }
            } else if (code == 0x2D) {
                if (dirty && previous != 5) {
                    status = 5;
                } else {
                    quit = 1;
                }
            }
            continue;
        }

        if (was_extended) {
            if (code == 0x4B) {
                if (cursor > 0) {
                    cursor--;
                }
            } else if (code == 0x4D) {
                if (cursor < length) {
                    cursor++;
                }
            } else if (code == 0x48) {
                int start = line_start(text, cursor);
                if (start > 0) {
                    int column = cursor - start;
                    int previous_start = line_start(text, start - 1);
                    int previous_length = start - 1 - previous_start;
                    if (column > previous_length) {
                        column = previous_length;
                    }
                    cursor = previous_start + column;
                }
            } else if (code == 0x50) {
                int start = line_start(text, cursor);
                int end = line_end(text, length, cursor);
                if (end < length) {
                    int column = cursor - start;
                    int next_end = line_end(text, length, end + 1);
                    int next_length = next_end - (end + 1);
                    if (column > next_length) {
                        column = next_length;
                    }
                    cursor = end + 1 + column;
                }
            } else if (code == 0x47) {
                cursor = line_start(text, cursor);
            } else if (code == 0x4F) {
                cursor = line_end(text, length, cursor);
            } else if (code == 0x53) {
                if (cursor < length) {
                    for (int i = cursor; i < length - 1; i++) {
                        text[i] = text[i + 1];
                    }
                    length--;
                    dirty = 1;
                }
            }
            continue;
        }

        if (code == 0x0E) {
            if (cursor > 0) {
                for (int i = cursor - 1; i < length - 1; i++) {
                    text[i] = text[i + 1];
                }
                length--;
                cursor--;
                dirty = 1;
            }
            continue;
        }

        int ch = 0;
        if (code == 0x1C) {
            ch = '\n';
        } else if (code == 0x0F) {
            ch = ' ';
        } else {
            ch = key_char(code, shift);
            if (caps) {
                if (ch >= 'a' && ch <= 'z') {
                    ch = ch - 32;
                } else if (ch >= 'A' && ch <= 'Z') {
                    ch = ch + 32;
                }
            }
        }
        if (ch == 0) {
            redraw = 0;
            status = previous;
            continue;
        }
        if (length >= 1000) {
            status = 6;
            continue;
        }
        for (int i = length; i > cursor; i--) {
            text[i] = text[i - 1];
        }
        text[cursor] = ch;
        length++;
        cursor++;
        dirty = 1;
    }

    terminal_clear();
    return 0;
}
