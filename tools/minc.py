"""minc: a minimal C compiler for MinOS.

Compiles a single C source file into a MinOS MEXE executable:

    .c -> minc -> .asm (includes the runtime library) -> NASM -> .EXE

Supported subset: int/char/void (with `const` and pointers), functions with
cdecl calls, local variables, if/else, while, for, break, continue, return,
string and character literals, and the usual arithmetic, comparison, logical,
bitwise, assignment, `*p`, `p[i]` and `&local` expressions. `#include <...>`,
`#include "..."`, flag `#define NAME` and `#ifndef/#ifdef/#endif` are handled.
Declarations without a body (such as those in minos.h) refer to functions
supplied by the runtime library, which uses the cdecl convention, returns in
EAX, preserves EDI (the image base) and is named with a leading underscore.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path


class CompileError(Exception):
    pass


TOKEN_PATTERN = re.compile(
    r"\s+"
    r"|(?P<number>0[xX][0-9a-fA-F]+|[0-9]+)"
    r"|(?P<identifier>[A-Za-z_][A-Za-z0-9_]*)"
    r'|(?P<string>"(?:\\.|[^"\\])*")'
    r"|(?P<char>'(?:\\.|[^'\\])')"
    r"|(?P<operator>\.\.\.|<<|>>|<=|>=|==|!=|&&|\|\||[{}\[\]();,=+\-*/%<>!&|^~])"
)

ESCAPES = {"n": 10, "r": 13, "t": 9, "0": 0, "\\": 92, "'": 39, '"': 34}
TYPE_KEYWORDS = {"int", "char", "void", "const", "unsigned", "long", "short"}


def preprocess(path, include_dirs, defined=None, stack=()):
    defined = set() if defined is None else defined
    path = Path(path)
    if path in stack:
        raise CompileError(f"{path}: recursive include")
    try:
        text = path.read_text()
    except OSError as error:
        raise CompileError(f"cannot read {path}: {error}")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)

    output = []
    active = [True]
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped.startswith("#"):
            if all(active):
                output.append((str(path), number, line))
            continue
        directive = stripped[1:].split(None, 1)
        name = directive[0] if directive else ""
        rest = directive[1].strip() if len(directive) > 1 else ""
        if name == "ifndef":
            active.append(rest not in defined)
        elif name == "ifdef":
            active.append(rest in defined)
        elif name == "endif":
            if len(active) == 1:
                raise CompileError(f"{path}:{number}: unmatched #endif")
            active.pop()
        elif not all(active):
            continue
        elif name == "define":
            if len(rest.split()) != 1:
                raise CompileError(
                    f"{path}:{number}: only flag #define is supported"
                )
            defined.add(rest)
        elif name == "include":
            match = re.fullmatch(r'<(.+)>|"(.+)"', rest)
            if not match:
                raise CompileError(f"{path}:{number}: bad #include")
            target = match.group(1) or match.group(2)
            candidates = [path.parent / target] if match.group(2) else []
            candidates += [Path(d) / target for d in include_dirs]
            found = next((c for c in candidates if c.is_file()), None)
            if found is None:
                raise CompileError(f"{path}:{number}: cannot find {target}")
            output.extend(
                preprocess(found, include_dirs, defined, stack + (path,))
            )
        else:
            raise CompileError(f"{path}:{number}: unsupported #{name}")
    if len(active) != 1:
        raise CompileError(f"{path}: missing #endif")
    return output


def tokenize(lines):
    tokens = []
    for file_name, number, line in lines:
        position = 0
        while position < len(line):
            match = TOKEN_PATTERN.match(line, position)
            if not match:
                raise CompileError(
                    f"{file_name}:{number}: unexpected {line[position]!r}"
                )
            position = match.end()
            kind = match.lastgroup
            if kind is not None:
                tokens.append((kind, match.group(kind), file_name, number))
    tokens.append(("eof", "", "", 0))
    return tokens


def decode_literal(literal):
    result = []
    body = literal[1:-1]
    index = 0
    while index < len(body):
        character = body[index]
        if character == "\\":
            index += 1
            escape = body[index]
            if escape not in ESCAPES:
                raise CompileError(f"unknown escape \\{escape}")
            result.append(ESCAPES[escape])
        else:
            result.append(ord(character))
        index += 1
    return result


# A type is (base, pointer_depth); only char* has byte-sized elements.
INT = ("int", 0)
CHAR_POINTER = ("char", 1)


def element_size(type_):
    return 1 if type_ == CHAR_POINTER else 4


class Compiler:
    PRECEDENCE = {
        "||": 1, "&&": 2, "|": 3, "^": 4, "&": 5, "==": 6, "!=": 6,
        "<": 7, "<=": 7, ">": 7, ">=": 7, "<<": 8, ">>": 8,
        "+": 9, "-": 9, "*": 10, "/": 10, "%": 10,
    }
    SETCC = {"==": "e", "!=": "ne", "<": "l", "<=": "le", ">": "g", ">=": "ge"}

    def __init__(self, tokens):
        self.tokens = tokens
        self.position = 0
        self.lines = []
        self.strings = []
        self.functions = {}
        self.defined = set()
        self.labels = 0
        self.body = []
        # (element size) when the last expression was a memory lvalue whose
        # address is in ECX.
        self.lvalue = None

    # Token helpers
    def peek(self, offset=0):
        return self.tokens[min(self.position + offset, len(self.tokens) - 1)]

    def error(self, message):
        token = self.peek()
        raise CompileError(f"{token[2]}:{token[3]}: {message}")

    def accept(self, value):
        token = self.peek()
        if token[1] == value and token[0] in ("operator", "identifier"):
            self.position += 1
            return True
        return False

    def expect(self, value):
        if not self.accept(value):
            self.error(f"expected {value!r}, found {self.peek()[1]!r}")

    def expect_identifier(self):
        token = self.peek()
        if token[0] != "identifier":
            self.error("expected an identifier")
        self.position += 1
        return token[1]

    def label(self, hint):
        self.labels += 1
        return f"__{hint}_{self.labels}"

    def emit(self, text):
        self.body.append("    " + text)

    def mark(self, label):
        self.body.append(label + ":")

    # Declarations
    def at_type(self):
        token = self.peek()
        return token[0] == "identifier" and token[1] in TYPE_KEYWORDS

    def parse_type(self):
        base = None
        while self.at_type():
            word = self.peek()[1]
            self.position += 1
            if word in ("int", "char", "void"):
                base = word
            elif word != "const" and base is None:
                base = "int"
        depth = 0
        while self.accept("*"):
            depth += 1
            self.accept("const")
        return (base or "int", depth)

    def compile(self):
        while self.peek()[0] != "eof":
            self.parse_external()
        if "main" not in self.defined:
            raise CompileError("program does not define main")
        return "\n".join(self.lines) + "\n"

    def parse_external(self):
        if not self.at_type():
            self.error("expected a declaration")
        return_type = self.parse_type()
        name = self.expect_identifier()
        if not self.accept("("):
            self.error("only function declarations are supported at file scope")
        params = []
        variadic = False
        if not self.accept(")"):
            while True:
                if self.accept("..."):
                    variadic = True
                    break
                param_type = self.parse_type()
                if param_type == ("void", 0) and self.peek()[1] == ")":
                    break
                param_name = ""
                if self.peek()[0] == "identifier":
                    param_name = self.expect_identifier()
                params.append((param_type, param_name))
                if not self.accept(","):
                    break
            self.expect(")")
        known = self.functions.get(name)
        if known is not None and known[1:] != (len(params), variadic):
            self.error(f"conflicting declaration of {name}")
        self.functions[name] = (return_type, len(params), variadic)
        if self.accept(";"):
            return
        if name in self.defined:
            self.error(f"redefinition of {name}")
        self.defined.add(name)
        self.compile_function(name, params)

    # Functions and statements
    def compile_function(self, name, params):
        self.body = []
        self.scopes = [{}]
        self.local_count = 0
        self.loops = []
        self.return_label = self.label("return")
        for index, (param_type, param_name) in enumerate(params):
            if not param_name:
                self.error("parameter name omitted in definition")
            self.scopes[0][param_name] = (8 + 4 * index, param_type)
        self.expect("{")
        self.parse_block_body()
        self.emit("xor eax, eax")
        self.mark(self.return_label)
        self.emit("mov esp, ebp")
        self.emit("pop ebp")
        self.emit("ret")
        self.lines.append(f"_{name}:")
        self.lines.append("    push ebp")
        self.lines.append("    mov ebp, esp")
        if self.local_count:
            self.lines.append(f"    sub esp, {4 * self.local_count}")
        self.lines.extend(self.body)
        self.lines.append("")

    def parse_block_body(self):
        self.scopes.append({})
        while not self.accept("}"):
            if self.peek()[0] == "eof":
                self.error("unexpected end of file; missing '}'")
            self.parse_statement()
        self.scopes.pop()

    def declare(self, name, type_):
        self.local_count += 1
        self.scopes[-1][name] = (-4 * self.local_count, type_)

    def lookup(self, name):
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        self.error(f"undeclared variable {name!r}")

    def parse_statement(self):
        if self.accept("{"):
            self.parse_block_body()
        elif self.at_type():
            self.parse_local_declaration()
        elif self.accept("if"):
            self.expect("(")
            self.parse_expression()
            self.expect(")")
            else_label, end_label = self.label("else"), self.label("endif")
            self.emit("test eax, eax")
            self.emit(f"jz {else_label}")
            self.parse_statement()
            if self.accept("else"):
                self.emit(f"jmp {end_label}")
                self.mark(else_label)
                self.parse_statement()
                self.mark(end_label)
            else:
                self.mark(else_label)
        elif self.accept("while"):
            top, end = self.label("while"), self.label("endwhile")
            self.mark(top)
            self.expect("(")
            self.parse_expression()
            self.expect(")")
            self.emit("test eax, eax")
            self.emit(f"jz {end}")
            self.loops.append((top, end))
            self.parse_statement()
            self.loops.pop()
            self.emit(f"jmp {top}")
            self.mark(end)
        elif self.accept("for"):
            self.parse_for()
        elif self.accept("return"):
            if self.accept(";"):
                self.emit("xor eax, eax")
            else:
                self.parse_expression()
                self.expect(";")
            self.emit(f"jmp {self.return_label}")
        elif self.peek()[0] == "identifier" and self.peek()[1] in ("break", "continue"):
            word = self.peek()[1]
            self.position += 1
            self.expect(";")
            if not self.loops:
                self.error(f"{word} outside of a loop")
            self.emit(f"jmp {self.loops[-1][0 if word == 'continue' else 1]}")
        elif self.accept(";"):
            pass
        else:
            self.parse_expression()
            self.expect(";")

    def parse_local_declaration(self):
        base_type = self.parse_type()
        while True:
            depth = base_type[1]
            while self.accept("*"):
                depth += 1
            name = self.expect_identifier()
            self.declare(name, (base_type[0], depth))
            if self.accept("="):
                self.parse_assignment()
                self.emit(f"mov [ebp{self.lookup(name)[0]:+d}], eax")
            if not self.accept(","):
                break
        self.expect(";")

    def parse_for(self):
        top, step = self.label("for"), self.label("forstep")
        end, body = self.label("endfor"), self.label("forbody")
        self.scopes.append({})
        self.expect("(")
        if self.at_type():
            self.parse_local_declaration()
        elif not self.accept(";"):
            self.parse_expression()
            self.expect(";")
        self.mark(top)
        if not self.accept(";"):
            self.parse_expression()
            self.expect(";")
            self.emit("test eax, eax")
            self.emit(f"jz {end}")
        self.emit(f"jmp {body}")
        self.mark(step)
        if not self.accept(")"):
            self.parse_expression()
            self.expect(")")
        self.emit(f"jmp {top}")
        self.mark(body)
        self.loops.append((step, end))
        self.parse_statement()
        self.loops.pop()
        self.emit(f"jmp {step}")
        self.mark(end)
        self.scopes.pop()

    # Expressions: each parse_* leaves its value in EAX and returns its type.
    def parse_expression(self):
        return self.parse_assignment()

    def parse_assignment(self):
        token, following = self.peek(), self.peek(1)
        if token[0] == "identifier" and following[0] == "operator" \
                and following[1] == "=":
            self.position += 2
            offset, type_ = self.lookup(token[1])
            self.parse_assignment()
            self.emit(f"mov [ebp{offset:+d}], eax")
            return type_
        type_ = self.parse_binary(0)
        if self.peek()[0] == "operator" and self.peek()[1] == "=":
            size = self.lvalue
            if size is None:
                self.error("left side of assignment is not assignable")
            self.position += 1
            self.emit("push ecx")
            self.parse_assignment()
            self.emit("pop ecx")
            self.emit("mov [ecx], al" if size == 1 else "mov [ecx], eax")
        return type_

    def parse_binary(self, minimum):
        left = self.parse_unary()
        while self.peek()[0] == "operator" and \
                self.PRECEDENCE.get(self.peek()[1], 0) > minimum:
            operator = self.peek()[1]
            precedence = self.PRECEDENCE[operator]
            self.position += 1
            if operator in ("&&", "||"):
                end = self.label("logic")
                self.emit("test eax, eax")
                self.emit(f"{'jz' if operator == '&&' else 'jnz'} {end}")
                self.parse_binary(precedence)
                self.mark(end)
                self.emit("test eax, eax")
                self.emit("setne al")
                self.emit("movzx eax, al")
                left = INT
            else:
                self.emit("push eax")
                right = self.parse_binary(precedence)
                self.emit("mov ecx, eax")
                self.emit("pop eax")
                left = self.emit_operator(operator, left, right)
            self.lvalue = None
        return left

    def emit_operator(self, operator, left, right):
        result = INT
        if operator in ("+", "-") and left[1] > 0 and right[1] == 0:
            if element_size(left) == 4:
                self.emit("shl ecx, 2")
            result = left
        if operator == "+":
            self.emit("add eax, ecx")
        elif operator == "-":
            self.emit("sub eax, ecx")
        elif operator == "*":
            self.emit("imul eax, ecx")
        elif operator in ("/", "%"):
            self.emit("cdq")
            self.emit("idiv ecx")
            if operator == "%":
                self.emit("mov eax, edx")
        elif operator == "&":
            self.emit("and eax, ecx")
        elif operator == "|":
            self.emit("or eax, ecx")
        elif operator == "^":
            self.emit("xor eax, ecx")
        elif operator == "<<":
            self.emit("shl eax, cl")
        elif operator == ">>":
            self.emit("sar eax, cl")
        else:
            self.emit("cmp eax, ecx")
            self.emit(f"set{self.SETCC[operator]} al")
            self.emit("movzx eax, al")
        return result

    def parse_unary(self):
        self.lvalue = None
        if self.accept("-"):
            self.parse_unary()
            self.emit("neg eax")
            self.lvalue = None
            return INT
        if self.accept("!"):
            self.parse_unary()
            self.emit("test eax, eax")
            self.emit("sete al")
            self.emit("movzx eax, al")
            self.lvalue = None
            return INT
        if self.accept("~"):
            self.parse_unary()
            self.emit("not eax")
            self.lvalue = None
            return INT
        if self.accept("+"):
            return self.parse_unary()
        if self.accept("*"):
            return self.dereference(self.parse_unary())
        if self.accept("&"):
            offset, type_ = self.lookup(self.expect_identifier())
            self.emit(f"lea eax, [ebp{offset:+d}]")
            self.lvalue = None
            return (type_[0], type_[1] + 1)
        return self.parse_postfix()

    def dereference(self, type_):
        if type_[1] == 0:
            self.error("cannot dereference a non-pointer")
        size = element_size(type_)
        self.emit("mov ecx, eax")
        self.emit("movsx eax, byte [ecx]" if size == 1 else "mov eax, [ecx]")
        self.lvalue = size
        return (type_[0], type_[1] - 1)

    def parse_postfix(self):
        type_ = self.parse_primary()
        while self.accept("["):
            self.emit("push eax")
            self.parse_expression()
            self.expect("]")
            if element_size(type_) == 4:
                self.emit("shl eax, 2")
            self.emit("pop ecx")
            self.emit("add eax, ecx")
            type_ = self.dereference(type_)
        return type_

    def parse_primary(self):
        kind, value, _, _ = self.peek()
        if kind == "number":
            self.position += 1
            self.emit(f"mov eax, {int(value, 0)}")
            return INT
        if kind == "char":
            self.position += 1
            self.emit(f"mov eax, {decode_literal(value)[0]}")
            return INT
        if kind == "string":
            data = []
            while self.peek()[0] == "string":
                data += decode_literal(self.peek()[1])
                self.position += 1
            self.strings.append(data)
            self.emit(
                f"lea eax, [edi + __string_{len(self.strings) - 1} - image_start]"
            )
            return CHAR_POINTER
        if self.accept("("):
            type_ = self.parse_expression()
            self.expect(")")
            self.lvalue = None
            return type_
        if kind == "identifier":
            name = self.expect_identifier()
            if self.accept("("):
                return self.parse_call(name)
            offset, type_ = self.lookup(name)
            self.emit(f"mov eax, [ebp{offset:+d}]")
            return type_
        self.error(f"unexpected {value!r} in expression")

    def parse_call(self, name):
        if name not in self.functions:
            self.error(f"call to undeclared function {name!r}")
        return_type, count, variadic = self.functions[name]
        argument_codes = []
        if not self.accept(")"):
            while True:
                saved, self.body = self.body, []
                self.parse_assignment()
                argument_codes.append(self.body)
                self.body = saved
                if not self.accept(","):
                    break
            self.expect(")")
        if len(argument_codes) < count or (
            not variadic and len(argument_codes) != count
        ):
            self.error(
                f"{name} expects {count} arguments, got {len(argument_codes)}"
            )
        for code in reversed(argument_codes):
            self.body.extend(code)
            self.emit("push eax")
        self.emit(f"call _{name}")
        if argument_codes:
            self.emit(f"add esp, {4 * len(argument_codes)}")
        self.lvalue = None
        return return_type

    def data_section(self):
        return [
            f"__string_{index}: db "
            + ", ".join(str(byte) for byte in data + [0])
            for index, data in enumerate(self.strings)
        ]


def compile_c(source, include_dirs, runtime):
    compiler = Compiler(tokenize(preprocess(source, include_dirs)))
    code = compiler.compile()
    return "\n".join(
        [
            "BITS 32",
            "ORG 0x40000000",
            f'%include "{Path(runtime).resolve().as_posix()}"',
            "",
            code,
            *compiler.data_section(),
            "image_end:",
            "",
        ]
    )


def main():
    parser = argparse.ArgumentParser(description="MinOS C compiler")
    parser.add_argument("source")
    parser.add_argument("-o", "--output", required=True, help="output .EXE")
    parser.add_argument("-I", dest="includes", action="append", default=[])
    parser.add_argument("--runtime", required=True, help="runtime library .asm")
    parser.add_argument("--nasm", default="nasm")
    parser.add_argument("--asm", help="where to write the generated assembly")
    arguments = parser.parse_args()

    output = Path(arguments.output)
    assembly = Path(arguments.asm) if arguments.asm else output.with_suffix(".asm")
    try:
        text = compile_c(arguments.source, arguments.includes, arguments.runtime)
    except CompileError as error:
        print(f"minc: {error}", file=sys.stderr)
        return 1
    assembly.parent.mkdir(parents=True, exist_ok=True)
    assembly.write_text(text)
    result = subprocess.run(
        [arguments.nasm, "-f", "bin", "-o", str(output), str(assembly)]
    )
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
