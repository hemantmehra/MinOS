#ifndef MINOS_H
#define MINOS_H

/* Prints prompt, then reads a line of at most max (<= 127) characters from
   the keyboard. Returns a pointer to a static buffer. */
char *read_line(const char *prompt, int max);

/* Parses decimal digits starting at text + offset. Returns -1 if there are
   no digits or any non-digit character. */
int parse_int(const char *text, int offset);

#endif
