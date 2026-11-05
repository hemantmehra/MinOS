"""minc: a minimal C compiler for MinOS.

Compiles a single C source file into a MinOS MEXE executable:

    .c -> minc -> .asm (includes the runtime library) -> NASM -> .EXE

Supported subset:

- types: int, char, void, float (32-bit, x87), pointers, local arrays and
  structs (`struct Name { ... };` at file scope; fields are 4-byte aligned);
- functions with cdecl calls, variadic declarations (`...`), recursion;
- statements: locals with initializers (including `{...}` lists for arrays and
  structs), if/else, while, for, switch/case/default, break, continue, return;
- expressions: arithmetic, comparison, logical, bitwise, assignment and
  compound assignment, `++`/`--`, `*p`, `&lvalue`, `p[i]`, `s.f`, `p->f`,
  pointer arithmetic, string, character and float literals. Float arguments to
  variadic functions are promoted to double as in C.

`#include <...>`, `#include "..."`, flag `#define NAME` and
`#ifndef/#ifdef/#endif` are handled. Declarations without a body (such as
those in stdio.h) refer to functions supplied by the runtime library, which
uses the cdecl convention, returns in EAX, preserves EDI (the image base) and
is named with a leading underscore.
"""

import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path


class CompileError(Exception):
    pass


TOKEN_PATTERN = re.compile(
    r"\s+"
    r"|(?P<float>[0-9]+\.[0-9]+[fF]?)"
    r"|(?P<number>0[xX][0-9a-fA-F]+|[0-9]+)"
    r"|(?P<identifier>[A-Za-z_][A-Za-z0-9_]*)"
    r'|(?P<string>"(?:\\.|[^"\\])*")'
    r"|(?P<char>'(?:\\.|[^'\\])')"
    r"|(?P<operator>\.\.\.|\+\+|--|\+=|-=|\*=|/=|->|<<|>>|<=|>=|==|!=|&&|\|\|"
    r"|[{}\[\]();,=+\-*/%<>!&|^~.:])"
)

ESCAPES = {"n": 10, "r": 13, "t": 9, "0": 0, "\\": 92, "'": 39, '"': 34}
TYPE_KEYWORDS = {
    "int", "char", "void", "float", "struct", "const", "unsigned", "long",
    "short",
}


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


# A type is (base, pointer_depth). Struct bases are "struct Name".
INT = ("int", 0)
CHAR = ("char", 0)
FLOAT = ("float", 0)
CHAR_POINTER = ("char", 1)


def is_float(type_):
    return type_ == FLOAT


def is_struct(type_):
    return type_[1] == 0 and type_[0].startswith("struct ")


class Compiler:
    PRECEDENCE = {
        "||": 1, "&&": 2, "|": 3, "^": 4, "&": 5, "==": 6, "!=": 6,
        "<": 7, "<=": 7, ">": 7, ">=": 7, "<<": 8, ">>": 8,
        "+": 9, "-": 9, "*": 10, "/": 10, "%": 10,
    }
    SETCC = {"==": "e", "!=": "ne", "<": "l", "<=": "le", ">": "g", ">=": "ge"}
    FLOAT_SETCC = {
        "==": "e", "!=": "ne", "<": "b", "<=": "be", ">": "a", ">=": "ae",
    }
    COMPOUND = {"+=": "+", "-=": "-", "*=": "*", "/=": "/"}

    def __init__(self, tokens):
        self.tokens = tokens
        self.position = 0
        self.lines = []
        self.strings = []
        self.functions = {}
        self.structs = {}
        self.defined = set()
        self.labels = 0
        self.body = []
        # True when the last expression is an lvalue: its address is in ECX
        # and its (loaded) value is in EAX.
        self.lvalue = False

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

    # Types
    def size_of(self, type_):
        if type_[1] > 0:
            return 4
        if type_ == CHAR:
            return 1
        if is_struct(type_):
            return self.structs[type_[0]][1]
        return 4

    def at_type(self):
        token = self.peek()
        return token[0] == "identifier" and token[1] in TYPE_KEYWORDS

    def parse_type(self):
        base = None
        while self.at_type():
            word = self.peek()[1]
            self.position += 1
            if word == "struct":
                name = "struct " + self.expect_identifier()
                if name not in self.structs:
                    self.error(f"unknown {name}")
                base = name
            elif word in ("int", "char", "void", "float"):
                base = word
            elif word != "const" and base is None:
                base = "int"
        depth = 0
        while self.accept("*"):
            depth += 1
            self.accept("const")
        return (base or "int", depth)

    def load(self, type_):
        """Loads the value at the address in ECX into EAX."""
        if is_struct(type_):
            self.emit("mov eax, ecx")
        elif type_ == CHAR:
            self.emit("movsx eax, byte [ecx]")
        else:
            self.emit("mov eax, [ecx]")

    def store(self, type_):
        """Stores EAX at the address in ECX."""
        if is_struct(type_):
            self.error("assigning whole structs is not supported")
        self.emit("mov [ecx], al" if type_ == CHAR else "mov [ecx], eax")

    def convert(self, source, target):
        if is_float(target) and not is_float(source) and source[1] == 0:
            self.emit("push eax")
            self.emit("fild dword [esp]")
            self.emit("fstp dword [esp]")
            self.emit("pop eax")
        elif is_float(source) and not is_float(target):
            self.emit("push eax")
            self.emit("fld dword [esp]")
            self.emit("fistp dword [esp]")
            self.emit("pop eax")

    # Top level
    def compile(self):
        while self.peek()[0] != "eof":
            self.parse_external()
        if "main" not in self.defined:
            raise CompileError("program does not define main")
        return "\n".join(self.lines) + "\n"

    def parse_struct_definition(self):
        self.expect("struct")
        name = "struct " + self.expect_identifier()
        if self.accept(";"):
            return
        self.expect("{")
        fields = {}
        offset = 0
        while not self.accept("}"):
            field_type = self.parse_type()
            while True:
                depth = field_type[1]
                while self.accept("*"):
                    depth += 1
                field = self.expect_identifier()
                type_ = (field_type[0], depth)
                fields[field] = (offset, type_)
                offset += (self.size_of(type_) + 3) // 4 * 4
                if not self.accept(","):
                    break
            self.expect(";")
        self.expect(";")
        self.structs[name] = (fields, offset)

    def parse_external(self):
        if self.peek()[1] == "struct" and self.peek(1)[0] == "identifier" \
                and self.peek(2)[1] in ("{", ";"):
            self.parse_struct_definition()
            return
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
                if self.accept("["):
                    self.expect("]")
                    param_type = (param_type[0], param_type[1] + 1)
                params.append((param_type, param_name))
                if not self.accept(","):
                    break
            self.expect(")")
        known = self.functions.get(name)
        if known is not None and known[1:3] != (len(params), variadic):
            self.error(f"conflicting declaration of {name}")
        self.functions[name] = (
            return_type, len(params), variadic, [p[0] for p in params]
        )
        if self.accept(";"):
            return
        if name in self.defined:
            self.error(f"redefinition of {name}")
        self.defined.add(name)
        self.compile_function(name, return_type, params)

    # Functions and statements
    def compile_function(self, name, return_type, params):
        self.body = []
        self.scopes = [{}]
        self.local_bytes = 0
        self.loops = []
        self.switches = []
        self.return_type = return_type
        self.return_label = self.label("return")
        for index, (param_type, param_name) in enumerate(params):
            if not param_name:
                self.error("parameter name omitted in definition")
            self.scopes[0][param_name] = (8 + 4 * index, param_type, None)
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
        if self.local_bytes:
            self.lines.append(f"    sub esp, {self.local_bytes}")
        self.lines.extend(self.body)
        self.lines.append("")

    def parse_block_body(self):
        self.scopes.append({})
        while not self.accept("}"):
            if self.peek()[0] == "eof":
                self.error("unexpected end of file; missing '}'")
            self.parse_statement()
        self.scopes.pop()

    def allocate(self, size):
        self.local_bytes += (size + 3) // 4 * 4
        return -self.local_bytes

    def declare(self, name, type_, array_length=None):
        size = self.size_of(type_)
        if array_length is not None:
            size *= array_length
        offset = self.allocate(size)
        self.scopes[-1][name] = (offset, type_, array_length)
        return offset

    def lookup(self, name):
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        self.error(f"undeclared variable {name!r}")

    def parse_constant(self):
        sign = -1 if self.accept("-") else 1
        kind, value, _, _ = self.peek()
        if kind == "number":
            self.position += 1
            return sign * int(value, 0)
        if kind == "char":
            self.position += 1
            return sign * decode_literal(value)[0]
        self.error("expected a constant")

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
        elif self.accept("switch"):
            self.parse_switch()
        elif self.accept("case"):
            if not self.switches:
                self.error("case outside of a switch")
            value = self.parse_constant()
            self.expect(":")
            label = self.label("case")
            self.mark(label)
            self.switches[-1]["cases"].append((value, label))
        elif self.accept("default"):
            if not self.switches:
                self.error("default outside of a switch")
            self.expect(":")
            label = self.label("default")
            self.mark(label)
            self.switches[-1]["default"] = label
        elif self.accept("return"):
            if self.accept(";"):
                self.emit("xor eax, eax")
            else:
                type_ = self.parse_expression()
                self.convert(type_, self.return_type)
                self.expect(";")
            self.emit(f"jmp {self.return_label}")
        elif self.peek()[0] == "identifier" and self.peek()[1] in ("break", "continue"):
            word = self.peek()[1]
            self.position += 1
            self.expect(";")
            if not self.loops:
                self.error(f"{word} outside of a loop or switch")
            target = self.loops[-1][0 if word == "continue" else 1]
            if target is None:
                self.error("continue outside of a loop")
            self.emit(f"jmp {target}")
        elif self.accept(";"):
            pass
        else:
            self.parse_expression()
            self.expect(";")

    def parse_switch(self):
        self.expect("(")
        self.parse_expression()
        self.expect(")")
        temp = self.allocate(4)
        dispatch, end = self.label("dispatch"), self.label("endswitch")
        self.emit(f"mov [ebp{temp:+d}], eax")
        self.emit(f"jmp {dispatch}")
        context = {"cases": [], "default": None}
        self.switches.append(context)
        self.loops.append((self.loops[-1][0] if self.loops else None, end))
        self.parse_statement()
        self.loops.pop()
        self.switches.pop()
        self.emit(f"jmp {end}")
        self.mark(dispatch)
        for value, label in context["cases"]:
            self.emit(f"cmp dword [ebp{temp:+d}], {value}")
            self.emit(f"je {label}")
        self.emit(f"jmp {context['default'] or end}")
        self.mark(end)

    def parse_local_declaration(self):
        base_type = self.parse_type()
        while True:
            depth = base_type[1]
            while self.accept("*"):
                depth += 1
            name = self.expect_identifier()
            type_ = (base_type[0], depth)
            array_length = None
            unsized = False
            if self.accept("["):
                if self.peek()[0] == "number":
                    array_length = int(self.peek()[1], 0)
                    self.position += 1
                else:
                    unsized = True
                self.expect("]")
            if self.accept("="):
                if self.accept("{"):
                    self.parse_initializer_list(
                        name, type_, array_length, unsized
                    )
                else:
                    if unsized:
                        self.error("array size required")
                    offset = self.declare(name, type_, array_length)
                    init_type = self.parse_assignment()
                    self.convert(init_type, type_)
                    self.emit(
                        f"mov [ebp{offset:+d}], "
                        + ("al" if type_ == CHAR else "eax")
                    )
            else:
                if unsized:
                    self.error("array size required")
                self.declare(name, type_, array_length)
            if not self.accept(","):
                break
        self.expect(";")

    def parse_initializer_list(self, name, type_, array_length, unsized):
        elements = []
        while not self.accept("}"):
            saved, self.body = self.body, []
            element_type = self.parse_assignment()
            elements.append((self.body, element_type))
            self.body = saved
            if not self.accept(",") and self.peek()[1] != "}":
                self.error("expected ',' or '}'")
        if is_struct(type_) and array_length is None:
            fields = sorted(
                self.structs[type_[0]][0].values(), key=lambda f: f[0]
            )
            targets = fields
        elif array_length is not None or unsized:
            if unsized:
                array_length = len(elements)
            targets = [(i * self.size_of(type_), type_) for i in range(array_length)]
        else:
            self.error("initializer list requires an array or struct")
        if len(elements) > len(targets):
            self.error("too many initializers")
        offset = self.declare(name, type_, array_length)
        total = self.size_of(type_) * (array_length or 1)
        for position in range(0, (total + 3) // 4 * 4, 4):
            self.emit(f"mov dword [ebp{offset + position:+d}], 0")
        for (code, element_type), (field_offset, field_type) in zip(elements, targets):
            self.body.extend(code)
            self.convert(element_type, field_type)
            self.emit(
                f"mov [ebp{offset + field_offset:+d}], "
                + ("al" if field_type == CHAR else "eax")
            )

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
        type_ = self.parse_binary(0)
        token = self.peek()
        if token[0] != "operator":
            return type_
        if token[1] == "=" or token[1] in self.COMPOUND:
            if not self.lvalue:
                self.error("left side of assignment is not assignable")
            self.position += 1
            self.emit("push ecx")
            if token[1] == "=":
                right = self.parse_assignment()
                self.convert(right, type_)
                self.emit("pop ecx")
            else:
                self.emit("push eax")
                right = self.parse_assignment()
                self.emit("mov ecx, eax")
                self.emit("pop eax")
                result = self.emit_operator(self.COMPOUND[token[1]], type_, right)
                self.convert(result, type_)
                self.emit("pop ecx")
            self.store(type_)
            self.lvalue = False
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
            self.lvalue = False
        return left

    def emit_operator(self, operator, left, right):
        if is_float(left) or is_float(right):
            return self.emit_float_operator(operator, left, right)
        result = INT
        if operator in ("+", "-") and left[1] > 0 and right[1] == 0:
            size = self.size_of((left[0], left[1] - 1))
            if size != 1:
                self.emit(f"imul ecx, ecx, {size}")
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

    def emit_float_operator(self, operator, left, right):
        if not is_float(left):
            self.convert(left, FLOAT)
        if not is_float(right):
            self.emit("xchg eax, ecx")
            self.convert(right, FLOAT)
            self.emit("xchg eax, ecx")
        self.emit("push ecx")
        self.emit("push eax")
        if operator in ("+", "-", "*", "/"):
            instruction = {"+": "fadd", "-": "fsub", "*": "fmul", "/": "fdiv"}
            self.emit("fld dword [esp]")
            self.emit(f"{instruction[operator]} dword [esp + 4]")
            self.emit("fstp dword [esp]")
            self.emit("pop eax")
            self.emit("add esp, 4")
            return FLOAT
        if operator not in self.FLOAT_SETCC:
            self.error(f"operator {operator!r} is not supported for float")
        self.emit("fld dword [esp + 4]")
        self.emit("fld dword [esp]")
        self.emit("fcomip st0, st1")
        self.emit("fstp st0")
        self.emit("add esp, 8")
        self.emit(f"set{self.FLOAT_SETCC[operator]} al")
        self.emit("movzx eax, al")
        return INT

    def parse_unary(self):
        self.lvalue = False
        if self.accept("-"):
            type_ = self.parse_unary()
            if is_float(type_):
                self.emit("xor eax, 0x80000000")
            else:
                self.emit("neg eax")
            self.lvalue = False
            return type_
        if self.accept("!"):
            self.parse_unary()
            self.emit("test eax, eax")
            self.emit("sete al")
            self.emit("movzx eax, al")
            self.lvalue = False
            return INT
        if self.accept("~"):
            self.parse_unary()
            self.emit("not eax")
            self.lvalue = False
            return INT
        if self.accept("+"):
            return self.parse_unary()
        if self.accept("*"):
            return self.dereference(self.parse_unary())
        if self.accept("&"):
            type_ = self.parse_unary()
            if self.lvalue:
                self.emit("mov eax, ecx")
            self.lvalue = False
            return (type_[0], type_[1] + 1)
        if self.peek()[1] in ("++", "--") and self.peek()[0] == "operator":
            operator = self.peek()[1]
            self.position += 1
            type_ = self.parse_unary()
            self.increment(type_, operator, prefix=True)
            return type_
        return self.parse_postfix()

    def increment(self, type_, operator, prefix):
        if not self.lvalue:
            self.error(f"operand of {operator} is not assignable")
        if is_float(type_):
            self.error(f"{operator} is not supported for float")
        step = self.size_of((type_[0], type_[1] - 1)) if type_[1] > 0 else 1
        instruction = "add" if operator == "++" else "sub"
        if not prefix:
            self.emit("push eax")
        self.emit(f"{instruction} eax, {step}")
        self.store(type_)
        if not prefix:
            self.emit("pop eax")
        self.lvalue = False

    def dereference(self, type_):
        if type_[1] == 0:
            self.error("cannot dereference a non-pointer")
        target = (type_[0], type_[1] - 1)
        self.emit("mov ecx, eax")
        self.load(target)
        self.lvalue = True
        return target

    def parse_postfix(self):
        type_ = self.parse_primary()
        while True:
            token = self.peek()
            if self.accept("["):
                self.emit("push eax")
                self.parse_expression()
                self.expect("]")
                size = self.size_of((type_[0], type_[1] - 1))
                if size != 1:
                    self.emit(f"imul eax, eax, {size}")
                self.emit("pop ecx")
                self.emit("add eax, ecx")
                type_ = self.dereference(type_)
            elif token[1] in (".", "->") and token[0] == "operator":
                self.position += 1
                field = self.expect_identifier()
                struct_type = type_ if token[1] == "." else (type_[0], type_[1] - 1)
                if not is_struct(struct_type) or (
                    token[1] == "->" and type_[1] != 1
                ):
                    self.error(f"{token[1]} applied to a non-struct")
                fields = self.structs[struct_type[0]][0]
                if field not in fields:
                    self.error(f"{struct_type[0]} has no field {field!r}")
                offset, type_ = fields[field]
                if offset:
                    self.emit(f"add eax, {offset}")
                self.emit("mov ecx, eax")
                self.load(type_)
                self.lvalue = True
            elif token[1] in ("++", "--") and token[0] == "operator":
                self.position += 1
                self.increment(type_, token[1], prefix=False)
            else:
                return type_

    def parse_primary(self):
        self.lvalue = False
        kind, value, _, _ = self.peek()
        if kind == "number":
            self.position += 1
            self.emit(f"mov eax, {int(value, 0)}")
            return INT
        if kind == "float":
            self.position += 1
            bits = struct.unpack("<I", struct.pack("<f", float(value.rstrip("fF"))))[0]
            self.emit(f"mov eax, {bits:#x}")
            return FLOAT
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
            self.lvalue = False
            return type_
        if kind == "identifier":
            name = self.expect_identifier()
            if self.accept("("):
                return self.parse_call(name)
            offset, type_, array_length = self.lookup(name)
            if array_length is not None:
                self.emit(f"lea eax, [ebp{offset:+d}]")
                return (type_[0], type_[1] + 1)
            self.emit(f"lea ecx, [ebp{offset:+d}]")
            self.load(type_)
            self.lvalue = True
            return type_
        self.error(f"unexpected {value!r} in expression")

    def parse_call(self, name):
        if name not in self.functions:
            self.error(f"call to undeclared function {name!r}")
        return_type, count, variadic, parameter_types = self.functions[name]
        argument_codes = []
        pushed = 0
        if not self.accept(")"):
            while True:
                saved, self.body = self.body, []
                index = len(argument_codes)
                type_ = self.parse_assignment()
                if index < count:
                    self.convert(type_, parameter_types[index])
                if is_float(type_) and index >= count:
                    # Variadic floats are promoted to double.
                    self.emit("push eax")
                    self.emit("fld dword [esp]")
                    self.emit("sub esp, 4")
                    self.emit("fstp qword [esp]")
                    pushed += 8
                else:
                    self.emit("push eax")
                    pushed += 4
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
        self.emit(f"call _{name}")
        if pushed:
            self.emit(f"add esp, {pushed}")
        self.lvalue = False
        return return_type

    def data_section(self):
        return [
            f"__string_{index}: db "
            + ", ".join(str(byte) for byte in data + [0])
            for index, data in enumerate(self.strings)
        ]


def compile_c(source, include_dirs, runtimes):
    compiler = Compiler(tokenize(preprocess(source, include_dirs)))
    code = compiler.compile()
    return "\n".join(
        [
            "BITS 32",
            "ORG 0x40000000",
            *[f'%include "{Path(r).resolve().as_posix()}"' for r in runtimes],
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
    parser.add_argument(
        "--runtime", required=True, action="append",
        help="runtime library .asm (repeatable; the first has the header)",
    )
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
