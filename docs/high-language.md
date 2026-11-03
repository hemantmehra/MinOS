# MinOS High

MinOS High is a small procedural language for bundled applications. During a
build, every `src/programs/*.high` file passes through these stages:

```text
.high -> tools/high_compiler.py -> build/programs/*.asm -> NASM -> build/*.EXE
```

The generated executables use the MinOS `MEXE` format and kernel system calls.
Generated assembly and executables live under `build/`; edit the `.high` source
instead of generated output.

## Syntax

Programs consist of functions. `main` is the entry point; other functions can
take integer parameters and use local integer variables.

```high
fn main() {
    let number = 5;
    let result = factorial(number);
    print("factorial = ");
    print_num(result);
    print("\n");
}

fn factorial(value) {
    let result = 1;
    let factor = 2;
    while (factor <= value) {
        result = result * factor;
        factor = factor + 1;
    }
    return result;
}
```

Statements include:

- `let name = expression;` to declare a local integer.
- `name = expression;` to update a local or parameter.
- `array name[size];` to declare a fixed-size local integer array. Accesses are
  bounds checked at runtime.
- `if (condition) { ... } else { ... }`, `else if`, and `while (condition) { ... }`.
- `return;` or `return expression;`.
- Function calls terminated with `;`.

Expressions support decimal integers, variables, function calls, parentheses,
unary `-` and `!`, arithmetic `+`, `-`, `*`, `/`, `%`, and comparisons `==`,
`!=`, `<`, `<=`, `>`, `>=`, and short-circuit `&&` and `||`. Division and
remainder use signed integer arithmetic. `//` and `#` start comments. Strings
support `\n`, `\r`, `\t`, `\"`, and `\\` escapes.

## Built-ins

- `print("text");` prints a string literal.
- `print_num(expression);` prints a decimal integer.
- `let value = read_number("prompt", max_digits);` reads and echoes digits,
  supports Backspace, and returns the entered number after Enter.
  `max_digits` must be a constant from 1 to 9.
- `let line = read_line("prompt", max_chars);` reads a lowercase command line,
  including digits, punctuation, and Shift-modified symbols; it echoes text,
  supports Backspace, and returns a pointer to its reusable input buffer.
  `max_chars` must be a constant from 1 to 127.
- `starts_with(line, "prefix")` and `equals(line, "text")` compare a line with
  a string literal.
- `parse_int(line, offset)` parses the decimal digits at a constant offset,
  returning `-1` if no valid integer begins there.
- `key()` returns a queued keyboard make or break scancode; break scancodes have
  bit 7 set, and Escape's make scancode is `1`.
- `string_char(line, index)` returns the byte at an index from 0 to 127, or
  zero when the index is outside that range or the pointer is outside the
  executable image.
- `print_char(value);` writes one character.
- `random(limit)` returns a value from `0` to `limit - 1` using the processor's
  timestamp counter; `limit` must be a positive constant.
- `read_arguments(array)` copies the command-line arguments into a local
  integer array of at least 128 elements and returns the character count, or
  `-1` if it does not fit.
- `opendir(path)` opens a directory by an absolute or current-directory-relative
  path and returns a stream handle, or `-1` on failure. `path` may be a string
  literal or a local integer array containing characters.
- `read_dir(handle, name)` reads the next name into a local integer array of at
  least 256 elements, returning `1` for an entry, `0` at end of directory, or
  `-1` on error. Each character is stored in one element.
  `closedir(handle);` closes the stream.
- `get_current_directory(array)` writes the current absolute path into a local
  integer array of at least 128 elements and returns its character count.
- `change_directory()` changes the current directory using the command-line
  path supplied when the program was launched; it returns `1` on success and
  `0` on failure. Empty paths keep the current directory.
- `clock();`, `memory_report();`, and `process_list();` show the current time,
  memory status, and active processes.
- `make_directory();` creates a root directory using the command-line arguments
  supplied when the program was launched.
- `terminal_clear();`, `start_graphics_mode();`, and `end_graphics_mode();`
  invoke primitive terminal and graphics services. `start_graphics_mode`
  initializes a white framebuffer; `end_graphics_mode` restores the terminal.
- `clear(color);` fills the 1280x720 framebuffer with an 8-bit palette color.
- `pixel(x, y, color);` draws one pixel when the coordinates are on screen.

`src/programs/ls.high` implements directory listing, hidden-name filtering, and
output formatting in High, using the directory stream services. It lists the
current directory by default and accepts an optional path. `pwd.high` prints
the current path, and `cd.high` changes it.
`src/programs/basic.high` implements the line editor, parser, and BASIC
interpreter in High, using character input/output primitives. `draw.high`
implements the circle rasterization in High, using pixel writes and graphics
start/restore services.

Integer values use 32-bit arithmetic; `print_num` prints signed decimal values.
Factorial is limited to 0 through 12 to fit in the kernel's 32-bit integer
arithmetic. Bundled `.high` programs demonstrate text output, loops, input,
functions, and framebuffer drawing.
