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
- `if (condition) { ... } else { ... }` and `while (condition) { ... }`.
- `return;` or `return expression;`.
- Function calls terminated with `;`.

Expressions support decimal integers, variables, function calls, parentheses,
unary `-` and `!`, arithmetic `+`, `-`, `*`, `/`, `%`, and comparisons `==`,
`!=`, `<`, `<=`, `>`, `>=`. Division and remainder use signed integer
arithmetic. `//` and `#` start comments. Strings support `\n`, `\r`, `\t`,
`\"`, and `\\` escapes.

## Built-ins

- `print("text");` prints a string literal.
- `print_num(expression);` prints a decimal integer.
- `let value = read_number("prompt", max_digits);` reads and echoes digits,
  supports Backspace, and returns the entered number after Enter.
  `max_digits` must be a constant from 1 to 9.
- `key()` returns a queued keyboard make scancode; Escape is `1`.
- `clear(color);` fills the 1280x720 framebuffer with an 8-bit palette color.
- `pixel(x, y, color);` draws one pixel when the coordinates are on screen.

Integer values use 32-bit arithmetic; `print_num` prints signed decimal values.
Factorial is limited to 0 through 12 to fit in the kernel's 32-bit integer
arithmetic. Bundled `.high` programs demonstrate text output, loops, input,
functions, and framebuffer drawing.
