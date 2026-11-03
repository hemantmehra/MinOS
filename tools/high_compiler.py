import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


TOKEN_PATTERN = re.compile(
    r"(?P<space>\s+)"
    r"|(?P<comment>//[^\n]*|#[^\n]*)"
    r'|(?P<string>"(?:\\.|[^"\\])*")'
    r"|(?P<number>[0-9]+)"
    r"|(?P<identifier>[A-Za-z_][A-Za-z0-9_]*)"
    r"|(?P<operator>==|!=|<=|>=|[{}\[\](),;=+\-*/%<>!])"
)

BUILTINS = {
    "clear": (1, "statement"),
    "equals": (2, "expression"),
    "key": (0, "expression"),
    "list_root": (0, "statement"),
    "parse_int": (2, "expression"),
    "pixel": (3, "statement"),
    "print": (1, "statement"),
    "print_num": (1, "statement"),
    "random": (1, "expression"),
    "read_number": (2, "expression"),
    "read_line": (2, "expression"),
    "starts_with": (2, "expression"),
}

PRECEDENCE = {
    "==": 1,
    "!=": 1,
    "<": 1,
    "<=": 1,
    ">": 1,
    ">=": 1,
    "+": 2,
    "-": 2,
    "*": 3,
    "/": 3,
    "%": 3,
}

CONDITION_CODES = {
    "==": "e",
    "!=": "ne",
    "<": "l",
    "<=": "le",
    ">": "g",
    ">=": "ge",
}


class CompileError(Exception):
    pass


@dataclass
class Token:
    kind: str
    value: object
    line: int
    column: int


class Lexer:
    def __init__(self, source):
        self.source = source

    def tokenize(self):
        tokens = []
        index = 0
        line = 1
        column = 1

        while index < len(self.source):
            match = TOKEN_PATTERN.match(self.source, index)
            if match is None:
                raise CompileError(
                    f"{line}:{column}: unexpected character "
                    f"{self.source[index]!r}"
                )

            text = match.group(0)
            kind = match.lastgroup
            token_line = line
            token_column = column
            index = match.end()

            if kind not in ("space", "comment"):
                if kind == "number":
                    value = int(text)
                elif kind == "string":
                    value = self._decode_string(text, token_line, token_column)
                else:
                    value = text
                tokens.append(Token(kind, value, token_line, token_column))

            newline_count = text.count("\n")
            if newline_count:
                line += newline_count
                column = len(text.rsplit("\n", 1)[-1]) + 1
            else:
                column += len(text)

        tokens.append(Token("eof", "", line, column))
        return tokens

    @staticmethod
    def _decode_string(text, line, column):
        escapes = {
            "n": "\n",
            "r": "\r",
            "t": "\t",
            '"': '"',
            "\\": "\\",
        }
        result = []
        index = 1
        while index < len(text) - 1:
            character = text[index]
            if character != "\\":
                result.append(character)
                index += 1
                continue
            index += 1
            if index >= len(text) - 1 or text[index] not in escapes:
                raise CompileError(
                    f"{line}:{column}: unsupported string escape"
                )
            result.append(escapes[text[index]])
            index += 1
        return "".join(result)


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.index = 0

    def parse(self):
        functions = []
        while not self._at("eof"):
            self._expect_value("fn")
            name = self._expect_kind("identifier").value
            self._expect_value("(")
            parameters = []
            if not self._at_value(")"):
                while True:
                    parameters.append(self._expect_kind("identifier").value)
                    if not self._accept_value(","):
                        break
            self._expect_value(")")
            body = self._parse_block()
            functions.append(
                {"name": name, "parameters": parameters, "body": body}
            )

        if not functions:
            self._error(self._current(), "program must define at least one function")
        return functions

    def _parse_block(self):
        self._expect_value("{")
        statements = []
        while not self._at_value("}"):
            if self._at("eof"):
                self._error(self._current(), "expected '}'")
            statements.append(self._parse_statement())
        self._expect_value("}")
        return statements

    def _parse_statement(self):
        token = self._current()
        if self._accept_value("let"):
            name = self._expect_kind("identifier").value
            self._expect_value("=")
            value = self._parse_expression()
            self._expect_value(";")
            return {"kind": "let", "name": name, "value": value, "token": token}

        if self._accept_value("array"):
            name = self._expect_kind("identifier").value
            self._expect_value("[")
            size = self._expect_kind("number").value
            self._expect_value("]")
            self._expect_value(";")
            if size < 1:
                self._error(token, "array size must be positive")
            return {"kind": "array", "name": name, "size": size, "token": token}

        if self._accept_value("if"):
            self._expect_value("(")
            condition = self._parse_expression()
            self._expect_value(")")
            then_body = self._parse_block()
            else_body = []
            if self._accept_value("else"):
                else_body = self._parse_block()
            return {
                "kind": "if",
                "condition": condition,
                "then": then_body,
                "else": else_body,
                "token": token,
            }

        if self._accept_value("while"):
            self._expect_value("(")
            condition = self._parse_expression()
            self._expect_value(")")
            return {
                "kind": "while",
                "condition": condition,
                "body": self._parse_block(),
                "token": token,
            }

        if self._accept_value("return"):
            value = None if self._at_value(";") else self._parse_expression()
            self._expect_value(";")
            return {"kind": "return", "value": value, "token": token}

        if self._current().kind == "identifier" and self._peek().value in (
            "=",
            "[",
        ):
            name = self._advance().value
            if self._accept_value("["):
                index = self._parse_expression()
                self._expect_value("]")
                self._expect_value("=")
                value = self._parse_expression()
                self._expect_value(";")
                return {
                    "kind": "array_assign",
                    "name": name,
                    "index": index,
                    "value": value,
                    "token": token,
                }
            self._expect_value("=")
            value = self._parse_expression()
            self._expect_value(";")
            return {"kind": "assign", "name": name, "value": value, "token": token}

        expression = self._parse_expression()
        self._expect_value(";")
        return {"kind": "expression", "value": expression, "token": token}

    def _parse_expression(self, minimum_precedence=0):
        left = self._parse_unary()
        while True:
            operator = self._current().value
            precedence = PRECEDENCE.get(operator, -1)
            if precedence < minimum_precedence:
                return left
            self._advance()
            right = self._parse_expression(precedence + 1)
            left = {
                "kind": "binary",
                "operator": operator,
                "left": left,
                "right": right,
            }

    def _parse_unary(self):
        token = self._current()
        if self._accept_value("-") or self._accept_value("!"):
            return {
                "kind": "unary",
                "operator": token.value,
                "value": self._parse_unary(),
            }
        return self._parse_primary()

    def _parse_primary(self):
        token = self._current()
        if self._accept_kind("number"):
            return {"kind": "number", "value": token.value}
        if self._accept_kind("string"):
            return {"kind": "string", "value": token.value, "token": token}
        if self._accept_value("("):
            expression = self._parse_expression()
            self._expect_value(")")
            return expression
        if self._accept_kind("identifier"):
            name = token.value
            if self._accept_value("["):
                index = self._parse_expression()
                self._expect_value("]")
                return {
                    "kind": "array_index",
                    "name": name,
                    "index": index,
                    "token": token,
                }
            if not self._accept_value("("):
                return {"kind": "variable", "name": name, "token": token}
            arguments = []
            if not self._at_value(")"):
                while True:
                    arguments.append(self._parse_expression())
                    if not self._accept_value(","):
                        break
            self._expect_value(")")
            return {
                "kind": "call",
                "name": name,
                "arguments": arguments,
                "token": token,
            }
        self._error(token, "expected an expression")

    def _at(self, kind):
        return self._current().kind == kind

    def _at_value(self, value):
        return self._current().value == value

    def _accept_value(self, value):
        if self._at_value(value):
            self._advance()
            return True
        return False

    def _accept_kind(self, kind):
        if self._at(kind):
            self._advance()
            return True
        return False

    def _expect_value(self, value):
        if not self._accept_value(value):
            self._error(self._current(), f"expected {value!r}")

    def _expect_kind(self, kind):
        if self._at(kind):
            return self._advance()
        self._error(self._current(), f"expected {kind}")

    def _current(self):
        return self.tokens[self.index]

    def _peek(self):
        return self.tokens[min(self.index + 1, len(self.tokens) - 1)]

    def _advance(self):
        token = self._current()
        self.index += 1
        return token

    @staticmethod
    def _error(token, message):
        raise CompileError(f"{token.line}:{token.column}: {message}")


class CodeGenerator:
    def __init__(self, functions):
        self.functions = functions
        self.function_map = {}
        self.strings = {}
        self.lines = []
        self.label_index = 0
        self.current_function = None

    def generate(self):
        self._validate_functions()
        self._line("BITS 32")
        self._line("ORG 0x40000000")
        self._line("")
        self._line("image_start:")
        self._line('    db "MEXE"')
        self._line("    dd image_end - image_start")
        self._line("    dd __entry - image_start")
        self._line("")
        self._line("__entry:")
        self._line("    cld")
        self._line("    call __get_image_base")
        self._line("__get_image_base:")
        self._line("    pop edi")
        self._line("    sub edi, __get_image_base - image_start")
        self._line("    call __fn_main")
        self._line("    mov eax, 0")
        self._line("    int 0x80")
        self._line("__halt:")
        self._line("    jmp __halt")
        self._line("")

        for function in self.functions:
            self._generate_function(function)

        self._generate_runtime()
        self._generate_data()
        self._line("image_end:")
        return "\n".join(self.lines) + "\n"

    def _validate_functions(self):
        for function in self.functions:
            name = function["name"]
            if name in self.function_map or name in BUILTINS:
                raise CompileError(f"duplicate or reserved function name: {name}")
            parameters = function["parameters"]
            if len(parameters) != len(set(parameters)):
                raise CompileError(f"duplicate parameter in function {name}")
            self.function_map[name] = function
        for function in self.functions:
            function["returns_value"] = self._statements_return_value(
                function["body"]
            )

        if "main" not in self.function_map:
            raise CompileError("program must define fn main()")
        if self.function_map["main"]["parameters"]:
            raise CompileError("fn main() cannot have parameters")

    def _generate_function(self, function):
        self.local_offsets = {}
        self.array_offsets = {}
        self.array_sizes = {}
        used_names = set(function["parameters"])
        self.local_cursor = -8
        self._allocate_locals(function["body"], used_names)
        self.parameter_offsets = {
            name: 8 + index * 4
            for index, name in enumerate(function["parameters"])
        }

        self._line(f"__fn_{function['name']}:")
        self._line("    push ebp")
        self._line("    mov ebp, esp")
        self._line("    push edi")
        local_bytes = -8 - self.local_cursor
        if local_bytes:
            self._line(f"    sub esp, {local_bytes}")
        self._generate_statements(function["body"])
        self._emit_epilogue()
        self._line("")

    def _allocate_locals(self, statements, used_names):
        for statement in statements:
            if statement["kind"] in ("let", "array"):
                name = statement["name"]
                if name in used_names:
                    self._error(statement["token"], f"duplicate variable {name!r}")
                used_names.add(name)
                if statement["kind"] == "let":
                    self.local_offsets[name] = self.local_cursor
                    self.local_cursor -= 4
                else:
                    size = statement["size"]
                    self.array_sizes[name] = size
                    self.array_offsets[name] = (
                        self.local_cursor - (size - 1) * 4
                    )
                    self.local_cursor -= size * 4
            elif statement["kind"] == "if":
                self._allocate_locals(statement["then"], used_names)
                self._allocate_locals(statement["else"], used_names)
            elif statement["kind"] == "while":
                self._allocate_locals(statement["body"], used_names)

    def _generate_statements(self, statements):
        for statement in statements:
            kind = statement["kind"]
            if kind == "let":
                self._generate_expression(statement["value"])
                self._store_variable(statement["name"], statement["token"])
            elif kind == "array":
                pass
            elif kind == "assign":
                self._generate_expression(statement["value"])
                self._store_variable(statement["name"], statement["token"])
            elif kind == "array_assign":
                self._store_array_element(statement)
            elif kind == "expression":
                self._generate_statement_call(statement["value"])
            elif kind == "return":
                if statement["value"] is not None:
                    self._generate_expression(statement["value"])
                else:
                    self._line("    xor eax, eax")
                self._emit_epilogue()
            elif kind == "if":
                self._generate_if(statement)
            elif kind == "while":
                self._generate_while(statement)

    def _generate_if(self, statement):
        otherwise = self._new_label("else")
        done = self._new_label("endif")
        self._generate_condition(statement["condition"])
        self._line(f"    jz {otherwise}")
        self._generate_statements(statement["then"])
        if statement["else"]:
            self._line(f"    jmp {done}")
            self._line(f"{otherwise}:")
            self._generate_statements(statement["else"])
            self._line(f"{done}:")
        else:
            self._line(f"{otherwise}:")

    def _generate_while(self, statement):
        start = self._new_label("while")
        done = self._new_label("endwhile")
        self._line(f"{start}:")
        self._generate_condition(statement["condition"])
        self._line(f"    jz {done}")
        self._generate_statements(statement["body"])
        self._line(f"    jmp {start}")
        self._line(f"{done}:")

    def _generate_condition(self, expression):
        self._generate_expression(expression)
        self._line("    test eax, eax")

    def _generate_statement_call(self, expression):
        if expression["kind"] != "call":
            self._error_from_expression(expression, "only calls can be used as statements")
        name = expression["name"]
        arguments = expression["arguments"]
        if name == "print":
            self._check_arity(expression, 1)
            argument = arguments[0]
            if argument["kind"] != "string":
                self._error(expression["token"], "print expects a string literal")
            label = self._intern_string(argument["value"])
            self._line("    mov eax, 1")
            self._line(f"    lea ebx, [edi + {label} - image_start]")
            self._line("    int 0x80")
            return
        if name == "print_num":
            self._check_arity(expression, 1)
            self._check_integer_arguments(expression)
            self._push_arguments(arguments)
            self._line("    call __high_print_num")
            self._discard_arguments(1)
            return
        if name in ("clear", "pixel"):
            expected = 1 if name == "clear" else 3
            self._check_arity(expression, expected)
            self._check_integer_arguments(expression)
            self._push_arguments(arguments)
            self._line(f"    call __high_{name}")
            self._discard_arguments(expected)
            return
        if name == "list_root":
            self._check_arity(expression, 0)
            self._line("    mov eax, 3")
            self._line("    int 0x80")
            return
        if name in BUILTINS:
            self._error(
                expression["token"],
                f"{name} cannot be used as a statement",
            )
        self._check_function_call(expression)
        self._push_arguments(arguments)
        self._line(f"    call __fn_{name}")
        self._discard_arguments(len(arguments))

    def _generate_expression(self, expression):
        kind = expression["kind"]
        if kind == "number":
            self._line(f"    mov eax, {expression['value']}")
        elif kind == "variable":
            self._load_variable(expression["name"], expression["token"])
        elif kind == "array_index":
            self._load_array_element(expression)
        elif kind == "unary":
            self._generate_expression(expression["value"])
            if expression["operator"] == "-":
                self._line("    neg eax")
            else:
                self._line("    test eax, eax")
                self._line("    sete al")
                self._line("    movzx eax, al")
        elif kind == "binary":
            self._generate_binary(expression)
        elif kind == "call":
            self._generate_expression_call(expression)
        elif kind == "string":
            self._error_from_expression(expression, "strings are only valid in print/read_number")

    def _generate_binary(self, expression):
        operator = expression["operator"]
        self._generate_expression(expression["left"])
        self._line("    push eax")
        self._generate_expression(expression["right"])
        self._line("    mov ecx, eax")
        self._line("    pop eax")
        if operator == "+":
            self._line("    add eax, ecx")
        elif operator == "-":
            self._line("    sub eax, ecx")
        elif operator == "*":
            self._line("    imul eax, ecx")
        elif operator in ("/", "%"):
            self._line("    cdq")
            self._line("    idiv ecx")
            if operator == "%":
                self._line("    mov eax, edx")
        else:
            self._line("    cmp eax, ecx")
            self._line(f"    set{CONDITION_CODES[operator]} al")
            self._line("    movzx eax, al")

    def _generate_expression_call(self, expression):
        name = expression["name"]
        arguments = expression["arguments"]
        if name == "key":
            self._check_arity(expression, 0)
            self._line("    mov eax, 2")
            self._line("    int 0x80")
        elif name == "read_number":
            self._check_arity(expression, 2)
            if arguments[0]["kind"] != "string":
                self._error(expression["token"], "read_number expects a string prompt")
            if (
                arguments[1]["kind"] != "number"
                or not 1 <= arguments[1]["value"] <= 9
            ):
                self._error(
                    expression["token"],
                    "read_number max_digits must be an integer from 1 to 9",
                )
            prompt = self._intern_string(arguments[0]["value"])
            self._generate_expression(arguments[1])
            self._line("    push eax")
            self._line(f"    push dword {prompt} - image_start")
            self._line("    call __high_read_number")
            self._discard_arguments(2)
        elif name == "read_line":
            self._check_arity(expression, 2)
            self._check_string_argument(expression, 0)
            self._check_constant_integer_argument(expression, 1, 1, 127)
            self._push_arguments(arguments)
            self._line("    call __high_read_line")
            self._discard_arguments(2)
        elif name == "starts_with":
            self._check_arity(expression, 2)
            self._check_string_argument(expression, 1)
            self._push_arguments(arguments)
            self._line("    call __high_starts_with")
            self._discard_arguments(2)
        elif name == "equals":
            self._check_arity(expression, 2)
            self._check_string_argument(expression, 1)
            self._push_arguments(arguments)
            self._line("    call __high_equals")
            self._discard_arguments(2)
        elif name == "parse_int":
            self._check_arity(expression, 2)
            self._check_constant_integer_argument(expression, 1, 0, 127)
            self._push_arguments(arguments)
            self._line("    call __high_parse_int")
            self._discard_arguments(2)
        elif name == "random":
            self._check_arity(expression, 1)
            self._check_constant_integer_argument(
                expression, 0, 1, 0x7FFFFFFF
            )
            self._push_arguments(arguments)
            self._line("    call __high_random")
            self._discard_arguments(1)
        elif name in BUILTINS:
            self._error(
                expression["token"],
                f"{name} does not return a value",
            )
        else:
            function = self._check_function_call(expression)
            if not function["returns_value"]:
                self._error(
                    expression["token"],
                    f"function {name!r} does not return a value on every path",
                )
            self._push_arguments(arguments)
            self._line(f"    call __fn_{name}")
            self._discard_arguments(len(arguments))

    def _push_arguments(self, arguments):
        for argument in reversed(arguments):
            if argument["kind"] == "string":
                label = self._intern_string(argument["value"])
                self._line(f"    push dword {label} - image_start")
            else:
                self._generate_expression(argument)
                self._line("    push eax")

    def _check_function_call(self, expression):
        name = expression["name"]
        function = self.function_map.get(name)
        if function is None:
            self._error(expression["token"], f"unknown function {name!r}")
        self._check_arity(expression, len(function["parameters"]))
        self._check_integer_arguments(
            expression, "user functions accept integer arguments only"
        )
        return function

    def _check_integer_arguments(
        self, expression, message="expected an integer argument"
    ):
        for argument in expression["arguments"]:
            if argument["kind"] == "string":
                self._error(expression["token"], message)

    def _check_string_argument(self, expression, index):
        if expression["arguments"][index]["kind"] != "string":
            self._error(
                expression["token"],
                f"{expression['name']} expects a string literal argument",
            )

    def _check_constant_integer_argument(self, expression, index, minimum, maximum):
        argument = expression["arguments"][index]
        if (
            argument["kind"] != "number"
            or argument["value"] < minimum
            or argument["value"] > maximum
        ):
            self._error(
                expression["token"],
                f"{expression['name']} argument {index + 1} must be a "
                f"constant from {minimum} to {maximum}",
            )

    def _load_array_element(self, expression):
        name = expression["name"]
        size = self.array_sizes.get(name)
        if size is None:
            self._error(expression["token"], f"unknown array {name!r}")
        self._generate_expression(expression["index"])
        self._emit_array_bounds_check(size)
        offset = self.array_offsets[name]
        self._line(f"    mov eax, [ebp+eax*4{offset:+d}]")

    def _store_array_element(self, statement):
        name = statement["name"]
        size = self.array_sizes.get(name)
        if size is None:
            self._error(statement["token"], f"unknown array {name!r}")
        self._generate_expression(statement["index"])
        self._line("    push eax")
        self._generate_expression(statement["value"])
        self._line("    mov ecx, eax")
        self._line("    pop eax")
        self._emit_array_bounds_check(size)
        offset = self.array_offsets[name]
        self._line(f"    mov [ebp+eax*4{offset:+d}], ecx")

    def _emit_array_bounds_check(self, size):
        self._line("    test eax, eax")
        self._line("    js __high_array_bounds_error")
        self._line(f"    cmp eax, {size}")
        self._line("    jae __high_array_bounds_error")

    @classmethod
    def _statements_return_value(cls, statements):
        for statement in statements:
            if statement["kind"] == "return":
                return statement["value"] is not None
            if statement["kind"] == "if" and statement["else"]:
                if (
                    cls._statements_return_value(statement["then"])
                    and cls._statements_return_value(statement["else"])
                ):
                    return True
        return False

    @staticmethod
    def _check_arity(expression, expected):
        actual = len(expression["arguments"])
        if actual != expected:
            raise CompileError(
                f"{expression['token'].line}:{expression['token'].column}: "
                f"{expression['name']} expects {expected} argument(s), got {actual}"
            )

    def _load_variable(self, name, token):
        offset = self.local_offsets.get(name)
        if offset is not None:
            self._line(f"    mov eax, [ebp{offset:+d}]")
            return
        offset = self.parameter_offsets.get(name)
        if offset is not None:
            self._line(f"    mov eax, [ebp+{offset}]")
            return
        self._error(token, f"unknown variable {name!r}")

    def _store_variable(self, name, token):
        offset = self.local_offsets.get(name)
        if offset is not None:
            self._line(f"    mov [ebp{offset:+d}], eax")
            return
        offset = self.parameter_offsets.get(name)
        if offset is not None:
            self._line(f"    mov [ebp+{offset}], eax")
            return
        self._error(token, f"unknown variable {name!r}")

    def _emit_epilogue(self):
        self._line("    mov edi, [ebp-4]")
        self._line("    mov esp, ebp")
        self._line("    pop ebp")
        self._line("    ret")

    def _generate_runtime(self):
        self._line("__high_print_num:")
        self._line("    push ebp")
        self._line("    mov ebp, esp")
        self._line("    push ebx")
        self._line("    push ecx")
        self._line("    push edx")
        self._line("    push esi")
        self._line("    push edi")
        self._line("    mov eax, [ebp+8]")
        self._line("    test eax, eax")
        self._line("    jns __high_print_num_positive")
        self._line("    neg eax")
        self._line("    push eax")
        self._line("    mov eax, 1")
        self._line("    lea ebx, [edi + __minus_sign - image_start]")
        self._line("    int 0x80")
        self._line("    pop eax")
        self._line("__high_print_num_positive:")
        self._line("    lea esi, [edi + __number_buffer - image_start + 10]")
        self._line("    mov byte [esi], 0")
        self._line("__high_print_num_digit:")
        self._line("    dec esi")
        self._line("    xor edx, edx")
        self._line("    mov ecx, 10")
        self._line("    div ecx")
        self._line("    add dl, '0'")
        self._line("    mov [esi], dl")
        self._line("    test eax, eax")
        self._line("    jnz __high_print_num_digit")
        self._line("    mov eax, 1")
        self._line("    mov ebx, esi")
        self._line("    int 0x80")
        self._line("    pop edi")
        self._line("    pop esi")
        self._line("    pop edx")
        self._line("    pop ecx")
        self._line("    pop ebx")
        self._line("    mov esp, ebp")
        self._line("    pop ebp")
        self._line("    ret")
        self._line("")

        self._line("__high_read_number:")
        self._line("    push ebp")
        self._line("    mov ebp, esp")
        self._line("    push ebx")
        self._line("    push esi")
        self._line("    push edi")
        self._line("    sub esp, 8")
        self._line("    mov dword [ebp-16], 0")
        self._line("    mov dword [ebp-20], 0")
        self._line("    mov eax, 1")
        self._line("    mov ebx, [ebp+8]")
        self._line("    add ebx, edi")
        self._line("    int 0x80")
        self._line("__high_read_number_key:")
        self._line("    mov eax, 2")
        self._line("    int 0x80")
        self._line("    test eax, eax")
        self._line("    jz __high_read_number_key")
        self._line("    cmp al, 0x1C")
        self._line("    je __high_read_number_done")
        self._line("    cmp al, 0x0E")
        self._line("    je __high_read_number_backspace")
        self._line("    cmp al, 0x02")
        self._line("    jb __high_read_number_key")
        self._line("    cmp al, 0x0B")
        self._line("    ja __high_read_number_key")
        self._line("    mov ecx, [ebp-20]")
        self._line("    cmp ecx, [ebp+12]")
        self._line("    jae __high_read_number_key")
        self._line("    cmp al, 0x0B")
        self._line("    jne __high_read_number_digit")
        self._line("    mov al, '0'")
        self._line("    jmp __high_read_number_store")
        self._line("__high_read_number_digit:")
        self._line("    add al, 0x2F")
        self._line("__high_read_number_store:")
        self._line("    mov [edi + __input_echo - image_start], al")
        self._line("    mov byte [edi + __input_echo_end - image_start], 0")
        self._line("    mov eax, [ebp-16]")
        self._line("    imul eax, 10")
        self._line("    mov [ebp-16], eax")
        self._line("    movzx eax, byte [edi + __input_echo - image_start]")
        self._line("    sub eax, '0'")
        self._line("    add [ebp-16], eax")
        self._line("    inc dword [ebp-20]")
        self._line("    mov eax, 1")
        self._line("    lea ebx, [edi + __input_echo - image_start]")
        self._line("    int 0x80")
        self._line("    jmp __high_read_number_key")
        self._line("__high_read_number_backspace:")
        self._line("    cmp dword [ebp-20], 0")
        self._line("    je __high_read_number_key")
        self._line("    dec dword [ebp-20]")
        self._line("    mov eax, [ebp-16]")
        self._line("    xor edx, edx")
        self._line("    mov ecx, 10")
        self._line("    div ecx")
        self._line("    mov [ebp-16], eax")
        self._line("    mov eax, 1")
        self._line("    lea ebx, [edi + __backspace_text - image_start]")
        self._line("    int 0x80")
        self._line("    jmp __high_read_number_key")
        self._line("__high_read_number_done:")
        self._line("    mov eax, 1")
        self._line("    lea ebx, [edi + __newline_text - image_start]")
        self._line("    int 0x80")
        self._line("    mov eax, [ebp-16]")
        self._line("    lea esp, [ebp-12]")
        self._line("    pop edi")
        self._line("    pop esi")
        self._line("    pop ebx")
        self._line("    pop ebp")
        self._line("    ret")
        self._line("")

        self._line("__high_read_line:")
        self._line("    push ebp")
        self._line("    mov ebp, esp")
        self._line("    push ebx")
        self._line("    push esi")
        self._line("    push edi")
        self._line("    sub esp, 8")
        self._line("    mov dword [ebp-16], 0")
        self._line("    mov eax, 1")
        self._line("    mov ebx, [ebp+8]")
        self._line("    add ebx, edi")
        self._line("    int 0x80")
        self._line("__high_read_line_key:")
        self._line("    mov eax, 2")
        self._line("    int 0x80")
        self._line("    test eax, eax")
        self._line("    jz __high_read_line_key")
        self._line("    cmp al, 0x1C")
        self._line("    je __high_read_line_done")
        self._line("    cmp al, 0x0E")
        self._line("    je __high_read_line_backspace")
        self._line("    cmp al, 0x39")
        self._line("    je __high_read_line_space")
        self._line("    cmp al, 0x02")
        self._line("    jb __high_read_line_key")
        self._line("    cmp al, 0x0B")
        self._line("    jbe __high_read_line_digit")
        self._line("    cmp al, 0x10")
        self._line("    jb __high_read_line_key")
        self._line("    cmp al, 0x19")
        self._line("    jbe __high_read_line_top_row")
        self._line("    cmp al, 0x1E")
        self._line("    jb __high_read_line_key")
        self._line("    cmp al, 0x26")
        self._line("    jbe __high_read_line_home_row")
        self._line("    cmp al, 0x2C")
        self._line("    jb __high_read_line_key")
        self._line("    cmp al, 0x32")
        self._line("    jbe __high_read_line_bottom_row")
        self._line("    jmp __high_read_line_key")
        self._line("__high_read_line_digit:")
        self._line("    cmp al, 0x0B")
        self._line("    jne __high_read_line_digit_nonzero")
        self._line("    mov al, '0'")
        self._line("    jmp __high_read_line_store")
        self._line("__high_read_line_digit_nonzero:")
        self._line("    add al, 0x2F")
        self._line("    jmp __high_read_line_store")
        self._line("__high_read_line_top_row:")
        self._line("    movzx eax, byte [edi + __top_row - image_start + eax - 0x10]")
        self._line("    jmp __high_read_line_store")
        self._line("__high_read_line_home_row:")
        self._line("    movzx eax, byte [edi + __home_row - image_start + eax - 0x1E]")
        self._line("    jmp __high_read_line_store")
        self._line("__high_read_line_bottom_row:")
        self._line("    movzx eax, byte [edi + __bottom_row - image_start + eax - 0x2C]")
        self._line("    jmp __high_read_line_store")
        self._line("__high_read_line_space:")
        self._line("    mov al, ' '")
        self._line("__high_read_line_store:")
        self._line("    mov ecx, [ebp-16]")
        self._line("    cmp ecx, [ebp+12]")
        self._line("    jae __high_read_line_key")
        self._line("    mov [edi + __line_buffer - image_start + ecx], al")
        self._line("    mov byte [edi + __line_buffer - image_start + ecx + 1], 0")
        self._line("    mov [edi + __input_echo - image_start], al")
        self._line("    mov byte [edi + __input_echo_end - image_start], 0")
        self._line("    inc dword [ebp-16]")
        self._line("    mov eax, 1")
        self._line("    lea ebx, [edi + __input_echo - image_start]")
        self._line("    int 0x80")
        self._line("    jmp __high_read_line_key")
        self._line("__high_read_line_backspace:")
        self._line("    cmp dword [ebp-16], 0")
        self._line("    je __high_read_line_key")
        self._line("    dec dword [ebp-16]")
        self._line("    mov ecx, [ebp-16]")
        self._line("    mov byte [edi + __line_buffer - image_start + ecx], 0")
        self._line("    mov eax, 1")
        self._line("    lea ebx, [edi + __backspace_text - image_start]")
        self._line("    int 0x80")
        self._line("    jmp __high_read_line_key")
        self._line("__high_read_line_done:")
        self._line("    mov eax, 1")
        self._line("    lea ebx, [edi + __newline_text - image_start]")
        self._line("    int 0x80")
        self._line("    lea eax, [edi + __line_buffer - image_start]")
        self._line("    lea esp, [ebp-12]")
        self._line("    pop edi")
        self._line("    pop esi")
        self._line("    pop ebx")
        self._line("    pop ebp")
        self._line("    ret")
        self._line("")

        self._line("__high_starts_with:")
        self._line("    push ebp")
        self._line("    mov ebp, esp")
        self._line("    push ebx")
        self._line("    push esi")
        self._line("    push edi")
        self._line("    mov esi, [ebp+8]")
        self._line("    mov ebx, [ebp+12]")
        self._line("    add ebx, edi")
        self._line("__high_starts_with_loop:")
        self._line("    mov al, [ebx]")
        self._line("    test al, al")
        self._line("    jz __high_starts_with_true")
        self._line("    cmp al, [esi]")
        self._line("    jne __high_starts_with_false")
        self._line("    inc ebx")
        self._line("    inc esi")
        self._line("    jmp __high_starts_with_loop")
        self._line("__high_starts_with_true:")
        self._line("    mov eax, 1")
        self._line("    jmp __high_starts_with_done")
        self._line("__high_starts_with_false:")
        self._line("    xor eax, eax")
        self._line("__high_starts_with_done:")
        self._line("    pop edi")
        self._line("    pop esi")
        self._line("    pop ebx")
        self._line("    mov esp, ebp")
        self._line("    pop ebp")
        self._line("    ret")
        self._line("")

        self._line("__high_equals:")
        self._line("    push ebp")
        self._line("    mov ebp, esp")
        self._line("    push ebx")
        self._line("    push esi")
        self._line("    push edi")
        self._line("    mov esi, [ebp+8]")
        self._line("    mov ebx, [ebp+12]")
        self._line("    add ebx, edi")
        self._line("__high_equals_loop:")
        self._line("    mov al, [ebx]")
        self._line("    cmp al, [esi]")
        self._line("    jne __high_equals_false")
        self._line("    test al, al")
        self._line("    jz __high_equals_true")
        self._line("    inc ebx")
        self._line("    inc esi")
        self._line("    jmp __high_equals_loop")
        self._line("__high_equals_true:")
        self._line("    mov eax, 1")
        self._line("    jmp __high_equals_done")
        self._line("__high_equals_false:")
        self._line("    xor eax, eax")
        self._line("__high_equals_done:")
        self._line("    pop edi")
        self._line("    pop esi")
        self._line("    pop ebx")
        self._line("    mov esp, ebp")
        self._line("    pop ebp")
        self._line("    ret")
        self._line("")

        self._line("__high_parse_int:")
        self._line("    push ebp")
        self._line("    mov ebp, esp")
        self._line("    push ebx")
        self._line("    push esi")
        self._line("    push edi")
        self._line("    mov esi, [ebp+8]")
        self._line("    add esi, [ebp+12]")
        self._line("    xor ebx, ebx")
        self._line("    xor ecx, ecx")
        self._line("__high_parse_int_loop:")
        self._line("    movzx eax, byte [esi]")
        self._line("    test al, al")
        self._line("    jz __high_parse_int_done")
        self._line("    cmp al, '0'")
        self._line("    jb __high_parse_int_invalid")
        self._line("    cmp al, '9'")
        self._line("    ja __high_parse_int_invalid")
        self._line("    imul ebx, ebx, 10")
        self._line("    sub eax, '0'")
        self._line("    add ebx, eax")
        self._line("    inc esi")
        self._line("    inc ecx")
        self._line("    jmp __high_parse_int_loop")
        self._line("__high_parse_int_done:")
        self._line("    test ecx, ecx")
        self._line("    jz __high_parse_int_invalid")
        self._line("    mov eax, ebx")
        self._line("    jmp __high_parse_int_return")
        self._line("__high_parse_int_invalid:")
        self._line("    mov eax, -1")
        self._line("__high_parse_int_return:")
        self._line("    pop edi")
        self._line("    pop esi")
        self._line("    pop ebx")
        self._line("    mov esp, ebp")
        self._line("    pop ebp")
        self._line("    ret")
        self._line("")

        self._line("__high_random:")
        self._line("    push ebp")
        self._line("    mov ebp, esp")
        self._line("    push ecx")
        self._line("    push edx")
        self._line("    mov ecx, [ebp+8]")
        self._line("    rdtsc")
        self._line("    xor eax, edx")
        self._line("    xor edx, edx")
        self._line("    div ecx")
        self._line("    mov eax, edx")
        self._line("    pop edx")
        self._line("    pop ecx")
        self._line("    mov esp, ebp")
        self._line("    pop ebp")
        self._line("    ret")
        self._line("")

        self._line("__high_clear:")
        self._line("    mov eax, [esp+4]")
        self._line("    push edi")
        self._line("    mov edi, 0xD0000000")
        self._line("    mov ecx, 1280 * 720")
        self._line("    rep stosb")
        self._line("    pop edi")
        self._line("    ret")
        self._line("")

        self._line("__high_pixel:")
        self._line("    push ebp")
        self._line("    mov ebp, esp")
        self._line("    mov eax, [ebp+8]")
        self._line("    test eax, eax")
        self._line("    js __high_pixel_done")
        self._line("    cmp eax, 1280")
        self._line("    jae __high_pixel_done")
        self._line("    mov ecx, [ebp+12]")
        self._line("    test ecx, ecx")
        self._line("    js __high_pixel_done")
        self._line("    cmp ecx, 720")
        self._line("    jae __high_pixel_done")
        self._line("    imul ecx, 1280")
        self._line("    add ecx, eax")
        self._line("    add ecx, 0xD0000000")
        self._line("    mov eax, [ebp+16]")
        self._line("    mov [ecx], al")
        self._line("__high_pixel_done:")
        self._line("    mov esp, ebp")
        self._line("    pop ebp")
        self._line("    ret")
        self._line("")
        self._line("__high_array_bounds_error:")
        self._line("    mov eax, 1")
        self._line("    lea ebx, [edi + __array_error - image_start]")
        self._line("    int 0x80")
        self._line("    mov eax, 0")
        self._line("    int 0x80")
        self._line("    jmp __halt")
        self._line("")

    def _generate_data(self):
        self._line("__number_buffer:")
        self._line("    times 11 db 0")
        self._line("__minus_sign:")
        self._line("    db '-', 0")
        self._line("__input_echo:")
        self._line("    db 0")
        self._line("__input_echo_end:")
        self._line("    db 0")
        self._line("__backspace_text:")
        self._line("    db 8, 0")
        self._line("__newline_text:")
        self._line("    db 10, 0")
        self._line("__array_error:")
        self._line('    db "High array index out of bounds.", 10, 0')
        self._line("__line_buffer:")
        self._line("    times 128 db 0")
        self._line("__top_row:")
        self._line('    db "qwertyuiop"')
        self._line("__home_row:")
        self._line('    db "asdfghjkl"')
        self._line("__bottom_row:")
        self._line('    db "zxcvbnm"')
        for value, label in self.strings.items():
            encoded = value.encode("utf-8") + b"\0"
            self._line(f"{label}:")
            self._line(f"    db {', '.join(str(byte) for byte in encoded)}")

    def _intern_string(self, value):
        if value not in self.strings:
            self.strings[value] = f"__string_{len(self.strings)}"
        return self.strings[value]

    def _new_label(self, prefix):
        self.label_index += 1
        return f"__{prefix}_{self.label_index}"

    def _discard_arguments(self, count):
        if count:
            self._line(f"    add esp, {count * 4}")

    def _line(self, text):
        self.lines.append(text)

    @staticmethod
    def _error(token, message):
        raise CompileError(f"{token.line}:{token.column}: {message}")

    @staticmethod
    def _error_from_expression(expression, message):
        token = expression.get("token")
        if token is None:
            raise CompileError(message)
        raise CompileError(f"{token.line}:{token.column}: {message}")


def compile_source(source):
    tokens = Lexer(source).tokenize()
    functions = Parser(tokens).parse()
    return CodeGenerator(functions).generate()


def main():
    parser = argparse.ArgumentParser(
        description="Compile a procedural MinOS .high program to NASM."
    )
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    try:
        assembly = compile_source(arguments.source.read_text(encoding="utf-8"))
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(assembly, encoding="utf-8", newline="\n")
    except (OSError, CompileError) as error:
        print(f"{arguments.source}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
