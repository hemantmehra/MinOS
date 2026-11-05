#include <stdio.h>
#include <string.h>
#include <minos.h>

/* Line-numbered BASIC interpreter: numbered lines, RUN, LIST, NEW, EXIT, PRINT,
   LET, IF ... THEN, GOTO, REM, END, STOP and integer expressions. */
int main() {
    int line_numbers[16];
    int program_text[2048];
    int variables[26];
    int values[128];
    int operators[128];
    int statement[132];
    char *input = 0;
    int line_count = 0;
    int finished = 0;
    int input_position = 0;
    int line_number = 0;
    int digit = 0;
    int line_index = 0;
    int existing_line = -1;
    int insert_at = 0;
    int copy_index = 0;
    int text_length = 0;
    int command_kind = 0;
    int execution_mode = 0;
    int current_line = -1;
    int next_line = 0;
    int step_count = 0;
    int halted = 0;
    int valid = 1;
    int character = 0;
    int quote_closed = 0;
    int variable_index = 0;
    int expression_mode = 0;
    int expression_phase = 0;
    int expression_position = 0;
    int expression_valid = 1;
    int expression_active = 0;
    int expect_operand = 1;
    int value_count = 0;
    int operator_count = 0;
    int number = 0;
    int operation = 0;
    int precedence = 0;
    int top_precedence = 0;
    int left_value = 0;
    int right_value = 0;
    int expression_value = 0;
    int reduction_active = 0;
    int comparison = 0;
    int condition = 0;
    int destination = -1;
    int target_number = 0;
    int target_valid = 0;
    int found = 0;
    int result = 0;
    int line_base = 0;
    int shift_index = 0;
    int command_length = 0;
    int immediate = 0;
    int quoted = 0;
    int output_index = 0;

    while (finished == 0) {
        input = read_line("basic> ", 127);
        command_kind = 0;
        input_position = 0;
        while (input[input_position] == 32 ||
            input[input_position] == 9) {
            input_position = input_position + 1;
        }

        if (input[input_position] == 0) {
        } else {
            line_number = 0;
            valid = 1;
            character = input[input_position];
            if (character >= 48 && character <= 57) {
                while (character >= 48 && character <= 57) {
                    digit = character - 48;
                    if (line_number > (9999 - digit) / 10) {
                        valid = 0;
                    } else {
                        line_number = line_number * 10 + digit;
                    }
                    input_position = input_position + 1;
                    character = input[input_position];
                }

                if (line_number == 0 ||
                    (character != 0 && character != 32 && character != 9)) {
                    valid = 0;
                }

                if (valid != 0) {
                    while (character == 32 || character == 9) {
                        input_position = input_position + 1;
                        character = input[input_position];
                    }

                    existing_line = -1;
                    line_index = 0;
                    while (line_index < line_count) {
                        if (line_numbers[line_index] == line_number) {
                            existing_line = line_index;
                        }
                        line_index = line_index + 1;
                    }

                    if (character == 0) {
                        if (existing_line >= 0) {
                            line_index = existing_line;
                            while (line_index + 1 < line_count) {
                                line_numbers[line_index] =
                                    line_numbers[line_index + 1];
                                copy_index = 0;
                                while (copy_index < 128) {
                                    program_text[line_index * 128 + copy_index] =
                                        program_text[(line_index + 1) * 128 + copy_index];
                                    copy_index = copy_index + 1;
                                }
                                line_index = line_index + 1;
                            }
                            line_count = line_count - 1;
                        }
                        command_kind = 6;
                    } else if (existing_line < 0 && line_count >= 16) {
                        printf("BASIC: program is full.\n");
                        command_kind = 6;
                    } else {
                        if (existing_line >= 0) {
                            insert_at = existing_line;
                        } else {
                            insert_at = 0;
                            while (insert_at < line_count &&
                                line_numbers[insert_at] < line_number) {
                                insert_at = insert_at + 1;
                            }
                            line_index = line_count;
                            while (line_index > insert_at) {
                                line_numbers[line_index] =
                                    line_numbers[line_index - 1];
                                copy_index = 0;
                                while (copy_index < 128) {
                                    program_text[line_index * 128 + copy_index] =
                                        program_text[(line_index - 1) * 128 + copy_index];
                                    copy_index = copy_index + 1;
                                }
                                line_index = line_index - 1;
                            }
                            line_count = line_count + 1;
                        }

                        line_numbers[insert_at] = line_number;
                        line_base = insert_at * 128;
                        text_length = 0;
                        character = input[input_position];
                        while (character != 0 && text_length < 127) {
                            program_text[line_base + text_length] = character;
                            text_length = text_length + 1;
                            input_position = input_position + 1;
                            character = input[input_position];
                        }
                        program_text[line_base + text_length] = 0;
                        if (character != 0) {
                            printf("BASIC: line is too long.\n");
                        }
                        command_kind = 6;
                    }
                } else {
                    command_kind = 0;
                }
            } else {
                command_kind = 0;
            }

            if (command_kind == 0 && valid == 0) {
                printf("BASIC: invalid command.\n");
                command_kind = 6;
            }

            if (command_kind == 0 && valid != 0) {
                if ((strcmp(input, "run") == 0)) {
                    command_kind = 1;
                } else {
                    if ((strcmp(input, "list") == 0)) {
                        command_kind = 2;
                    } else {
                        if ((strcmp(input, "new") == 0)) {
                            command_kind = 3;
                        } else {
                            if ((strcmp(input, "exit") == 0) ||
                                (strcmp(input, "quit") == 0)) {
                                command_kind = 4;
                            } else {
                                command_kind = 5;
                            }
                        }
                    }
                }
            }

            if (command_kind == 1) {
                line_index = 0;
                step_count = 0;
                line_index = 0;
                while (line_index < 26) {
                    variables[line_index] = 0;
                    line_index = line_index + 1;
                }
                line_index = 0;
                execution_mode = 1;
            } else {
                if (command_kind == 2) {
                    line_index = 0;
                    while (line_index < line_count) {
                        printf("%d", line_numbers[line_index]);
                        putchar(32);
                        copy_index = 0;
                        line_base = line_index * 128;
                        character = program_text[line_base + copy_index];
                        while (character != 0) {
                            putchar(character);
                            copy_index = copy_index + 1;
                            character = program_text[line_base + copy_index];
                        }
                        printf("\n");
                        line_index = line_index + 1;
                    }
                } else {
                    if (command_kind == 3) {
                        line_count = 0;
                        printf("BASIC: program cleared.\n");
                    } else {
                        if (command_kind == 4) {
                            finished = 1;
                        } else {
                            if (command_kind == 5) {
                                copy_index = 0;
                                while (copy_index < 128) {
                                    statement[copy_index] =
                                        input[copy_index];
                                    copy_index = copy_index + 1;
                                }
                                statement[128] = 0;
                                statement[129] = 0;
                                statement[130] = 0;
                                statement[131] = 0;
                                execution_mode = 2;
                                current_line = -1;
                            }
                        }
                    }
                }
            }

            while (execution_mode != 0) {
                if (execution_mode == 1) {
                    if (line_index >= line_count) {
                        execution_mode = 0;
                    } else {
                        if (step_count >= 10000) {
                            printf("BASIC: execution limit reached.\n");
                            execution_mode = 0;
                        } else {
                            step_count = step_count + 1;
                            current_line = line_index;
                            next_line = line_index + 1;
                            line_base = line_index * 128;
                            copy_index = 0;
                            character = program_text[line_base + copy_index];
                            while (copy_index < 127) {
                                statement[copy_index] = character;
                                copy_index = copy_index + 1;
                                character = program_text[line_base + copy_index];
                            }
                            statement[127] = 0;
                            statement[128] = 0;
                            statement[129] = 0;
                            statement[130] = 0;
                            statement[131] = 0;
                        }
                    }
                } else {
                    next_line = 0;
                }

                if (execution_mode != 0) {
                    input_position = 0;
                    while (statement[input_position] == 32 ||
                        statement[input_position] == 9) {
                        input_position = input_position + 1;
                    }
                    character = statement[input_position];
                    valid = 1;
                    halted = 0;
                    expression_mode = 0;
                    expression_phase = 0;
                    variable_index = 0;
                    immediate = execution_mode == 2;

                    if (character == 0) {
                        valid = 0;
                    } else if (character == 114 &&
                        statement[input_position + 1] == 101 &&
                        statement[input_position + 2] == 109 &&
                        (statement[input_position + 3] == 0 ||
                            statement[input_position + 3] == 32 ||
                            statement[input_position + 3] == 9)) {
                        expression_mode = 0;
                    } else if (character == 101 && (
                        statement[input_position + 1] == 110 &&
                        statement[input_position + 2] == 100 &&
                        (statement[input_position + 3] == 0 ||
                            statement[input_position + 3] == 32 ||
                            statement[input_position + 3] == 9))) {
                        input_position = input_position + 3;
                        while (statement[input_position] == 32 ||
                            statement[input_position] == 9) {
                            input_position = input_position + 1;
                        }
                        if (statement[input_position] != 0) {
                            valid = 0;
                        } else {
                            halted = 1;
                        }
                    } else if (character == 115 &&
                        statement[input_position + 1] == 116 &&
                        statement[input_position + 2] == 111 &&
                        statement[input_position + 3] == 112 &&
                        (statement[input_position + 4] == 0 ||
                            statement[input_position + 4] == 32 ||
                            statement[input_position + 4] == 9)) {
                        input_position = input_position + 4;
                        while (statement[input_position] == 32 ||
                            statement[input_position] == 9) {
                            input_position = input_position + 1;
                        }
                        if (statement[input_position] != 0) {
                            valid = 0;
                        } else {
                            halted = 1;
                        }
                    } else if (character == 112 &&
                        statement[input_position + 1] == 114 &&
                        statement[input_position + 2] == 105 &&
                        statement[input_position + 3] == 110 &&
                        statement[input_position + 4] == 116 &&
                        (statement[input_position + 5] == 0 ||
                            statement[input_position + 5] == 32 ||
                            statement[input_position + 5] == 9)) {
                        input_position = input_position + 5;
                        while (statement[input_position] == 32 ||
                            statement[input_position] == 9) {
                            input_position = input_position + 1;
                        }
                        if (statement[input_position] == 34) {
                            output_index = input_position + 1;
                            quote_closed = 0;
                            while (statement[output_index] != 0 &&
                                statement[output_index] != 34) {
                                putchar(statement[output_index]);
                                output_index = output_index + 1;
                            }
                            if (statement[output_index] == 34) {
                                quote_closed = 1;
                                output_index = output_index + 1;
                            }
                            while (statement[output_index] == 32 ||
                                statement[output_index] == 9) {
                                output_index = output_index + 1;
                            }
                            if (quote_closed == 0 ||
                                statement[output_index] != 0) {
                                valid = 0;
                            } else {
                                printf("\n");
                            }
                        } else {
                            expression_mode = 1;
                            expression_position = input_position;
                        }
                    } else if (character == 108 &&
                        statement[input_position + 1] == 101 &&
                        statement[input_position + 2] == 116 &&
                        (statement[input_position + 3] == 0 ||
                            statement[input_position + 3] == 32 ||
                            statement[input_position + 3] == 9)) {
                        input_position = input_position + 3;
                        while (statement[input_position] == 32 ||
                            statement[input_position] == 9) {
                            input_position = input_position + 1;
                        }
                        character = statement[input_position];
                        if (character < 97 || character > 122) {
                            valid = 0;
                        } else {
                            variable_index = character - 97;
                            input_position = input_position + 1;
                            while (statement[input_position] == 32 ||
                                statement[input_position] == 9) {
                                input_position = input_position + 1;
                            }
                            if (statement[input_position] != 61) {
                                valid = 0;
                            } else {
                                expression_mode = 2;
                                expression_position = input_position + 1;
                            }
                        }
                    } else if (character == 103 &&
                        statement[input_position + 1] == 111 &&
                        statement[input_position + 2] == 116 &&
                        statement[input_position + 3] == 111 &&
                        (statement[input_position + 4] == 0 ||
                            statement[input_position + 4] == 32 ||
                            statement[input_position + 4] == 9)) {
                        input_position = input_position + 4;
                        while (statement[input_position] == 32 ||
                            statement[input_position] == 9) {
                            input_position = input_position + 1;
                        }
                        target_number = 0;
                        target_valid = 0;
                        character = statement[input_position];
                        while (character >= 48 && character <= 57) {
                            target_valid = 1;
                            digit = character - 48;
                            if (target_number > (9999 - digit) / 10) {
                                valid = 0;
                            } else {
                                target_number = target_number * 10 + digit;
                            }
                            input_position = input_position + 1;
                            character = statement[input_position];
                        }
                        while (character == 32 || character == 9) {
                            input_position = input_position + 1;
                            character = statement[input_position];
                        }
                        if (target_valid == 0 || character != 0 ||
                            immediate != 0) {
                            valid = 0;
                        } else {
                            found = 0;
                            line_index = 0;
                            while (line_index < line_count) {
                                if (line_numbers[line_index] == target_number) {
                                    next_line = line_index;
                                    found = 1;
                                }
                                line_index = line_index + 1;
                            }
                            if (found == 0) {
                                printf("BASIC: line not found.\n");
                                halted = 1;
                            }
                        }
                    } else if (character == 105 &&
                        statement[input_position + 1] == 102 &&
                        (statement[input_position + 2] == 0 ||
                            statement[input_position + 2] == 32 ||
                            statement[input_position + 2] == 9)) {
                        if (immediate != 0) {
                            valid = 0;
                        } else {
                            expression_mode = 3;
                            expression_phase = 0;
                            expression_position = input_position + 2;
                        }
                    } else {
                        valid = 0;
                    }

                    while (expression_mode != 0 && valid != 0) {
                        input_position = expression_position;
                        expression_active = 1;
                        expression_valid = 1;
                        expect_operand = 1;
                        value_count = 0;
                        operator_count = 0;

                        while (expression_active != 0) {
                            character = statement[input_position];
                            if (character == 32 || character == 9) {
                                input_position = input_position + 1;
                            } else if (character >= 48 &&
                                character <= 57) {
                                number = 0;
                                while (character >= 48 && character <= 57) {
                                    digit = character - 48;
                                    if (number > (32767 - digit) / 10) {
                                        expression_valid = 0;
                                    } else {
                                        number = number * 10 + digit;
                                    }
                                    input_position = input_position + 1;
                                    character = statement[input_position];
                                }
                                if (expression_valid != 0 &&
                                    value_count < 128) {
                                    values[value_count] = number;
                                    value_count = value_count + 1;
                                    expect_operand = 0;
                                } else {
                                    expression_valid = 0;
                                }
                            } else if (expression_mode == 4 &&
                                character == 116 &&
                                statement[input_position + 1] == 104 &&
                                statement[input_position + 2] == 101 &&
                                statement[input_position + 3] == 110 &&
                                (statement[input_position + 4] == 0 ||
                                    statement[input_position + 4] == 32 ||
                                    statement[input_position + 4] == 9)) {
                                expression_active = 0;
                            } else if (character >= 97 &&
                                character <= 122) {
                                values[value_count] =
                                    variables[character - 97];
                                value_count = value_count + 1;
                                input_position = input_position + 1;
                                expect_operand = 0;
                            } else if (character == 40) {
                                operators[operator_count] = 40;
                                operator_count = operator_count + 1;
                                input_position = input_position + 1;
                                expect_operand = 1;
                            } else if (character == 41) {
                                reduction_active = 1;
                                while (reduction_active != 0) {
                                    if (operator_count == 0) {
                                        expression_valid = 0;
                                        reduction_active = 0;
                                    } else {
                                        operation =
                                            operators[operator_count - 1];
                                        if (operation == 40) {
                                            operator_count = operator_count - 1;
                                            reduction_active = 0;
                                        } else {
                                            operator_count = operator_count - 1;
                                            if (value_count < 2) {
                                                expression_valid = 0;
                                            } else {
                                                right_value =
                                                    values[value_count - 1];
                                                left_value =
                                                    values[value_count - 2];
                                                value_count = value_count - 2;
                                                if (operation == 43) {
                                                    result = left_value + right_value;
                                                } else if (operation == 45) {
                                                    result = left_value - right_value;
                                                } else if (operation == 42) {
                                                    result = left_value * right_value;
                                                } else if (right_value == 0) {
                                                    expression_valid = 0;
                                                    result = 0;
                                                } else {
                                                    result = left_value / right_value;
                                                }
                                                if (result > 32767 ||
                                                    result < -32767) {
                                                    expression_valid = 0;
                                                }
                                                values[value_count] = result;
                                                value_count = value_count + 1;
                                            }
                                        }
                                    }
                                }
                                input_position = input_position + 1;
                                expect_operand = 0;
                            } else if (character == 43 ||
                                character == 45 ||
                                character == 42 ||
                                character == 47) {
                                if (expect_operand != 0) {
                                    if (character == 45) {
                                        operators[operator_count] = 126;
                                        operator_count = operator_count + 1;
                                    }
                                    input_position = input_position + 1;
                                } else {
                                    if (character == 43 || character == 45) {
                                        precedence = 1;
                                    } else {
                                        precedence = 2;
                                    }
                                    reduction_active = 1;
                                    while (reduction_active != 0) {
                                        if (operator_count == 0) {
                                            reduction_active = 0;
                                        } else {
                                            operation =
                                                operators[operator_count - 1];
                                            if (operation == 40) {
                                                reduction_active = 0;
                                            } else {
                                                if (operation == 126) {
                                                    top_precedence = 3;
                                                } else if (operation == 43 ||
                                                    operation == 45) {
                                                    top_precedence = 1;
                                                } else {
                                                    top_precedence = 2;
                                                }
                                                if (top_precedence < precedence) {
                                                    reduction_active = 0;
                                                } else {
                                                    operator_count =
                                                        operator_count - 1;
                                                    if (operation == 126) {
                                                        if (value_count < 1) {
                                                            expression_valid = 0;
                                                        } else {
                                                            values[value_count - 1] =
                                                                0 - values[value_count - 1];
                                                        }
                                                    } else if (value_count < 2) {
                                                        expression_valid = 0;
                                                    } else {
                                                        right_value =
                                                            values[value_count - 1];
                                                        left_value =
                                                            values[value_count - 2];
                                                        value_count = value_count - 2;
                                                        if (operation == 43) {
                                                            result = left_value + right_value;
                                                        } else if (operation == 45) {
                                                            result = left_value - right_value;
                                                        } else if (operation == 42) {
                                                            result = left_value * right_value;
                                                        } else if (right_value == 0) {
                                                            expression_valid = 0;
                                                            result = 0;
                                                        } else {
                                                            result = left_value / right_value;
                                                        }
                                                        if (result > 32767 ||
                                                            result < -32767) {
                                                            expression_valid = 0;
                                                        }
                                                        values[value_count] = result;
                                                        value_count = value_count + 1;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    operators[operator_count] = character;
                                    operator_count = operator_count + 1;
                                    input_position = input_position + 1;
                                    expect_operand = 1;
                                }
                            } else {
                                expression_active = 0;
                            }
                        }

                        if (expect_operand != 0) {
                            expression_valid = 0;
                        }

                        reduction_active = 1;
                        while (reduction_active != 0) {
                            if (operator_count == 0) {
                                reduction_active = 0;
                            } else {
                                operation = operators[operator_count - 1];
                                operator_count = operator_count - 1;
                                if (operation == 40) {
                                    expression_valid = 0;
                                } else if (operation == 126) {
                                    if (value_count < 1) {
                                        expression_valid = 0;
                                    } else {
                                        values[value_count - 1] =
                                            0 - values[value_count - 1];
                                    }
                                } else if (value_count < 2) {
                                    expression_valid = 0;
                                } else {
                                    right_value = values[value_count - 1];
                                    left_value = values[value_count - 2];
                                    value_count = value_count - 2;
                                    if (operation == 43) {
                                        result = left_value + right_value;
                                    } else if (operation == 45) {
                                        result = left_value - right_value;
                                    } else if (operation == 42) {
                                        result = left_value * right_value;
                                    } else if (right_value == 0) {
                                        expression_valid = 0;
                                        result = 0;
                                    } else {
                                        result = left_value / right_value;
                                    }
                                    if (result > 32767 || result < -32767) {
                                        expression_valid = 0;
                                    }
                                    values[value_count] = result;
                                    value_count = value_count + 1;
                                }
                            }
                        }

                        if (value_count != 1) {
                            expression_valid = 0;
                        } else {
                            expression_value = values[0];
                        }

                        if (expression_valid == 0) {
                            valid = 0;
                        } else if (expression_mode == 1) {
                            while (statement[input_position] == 32 ||
                                statement[input_position] == 9) {
                                input_position = input_position + 1;
                            }
                            if (statement[input_position] != 0) {
                                valid = 0;
                            } else {
                                printf("%d", expression_value);
                                printf("\n");
                                expression_mode = 0;
                            }
                        } else if (expression_mode == 2) {
                            while (statement[input_position] == 32 ||
                                statement[input_position] == 9) {
                                input_position = input_position + 1;
                            }
                            if (statement[input_position] != 0) {
                                valid = 0;
                            } else {
                                variables[variable_index] = expression_value;
                                expression_mode = 0;
                            }
                        } else if (expression_mode == 3 &&
                            expression_phase == 0) {
                            left_value = expression_value;
                            character = statement[input_position];
                            comparison = 0;
                            if (character == 61) {
                                comparison = 1;
                                input_position = input_position + 1;
                            } else if (character == 60) {
                                input_position = input_position + 1;
                                if (statement[input_position] == 62) {
                                    comparison = 6;
                                    input_position = input_position + 1;
                                } else if (statement[input_position] == 61) {
                                    comparison = 4;
                                    input_position = input_position + 1;
                                } else {
                                    comparison = 2;
                                }
                            } else if (character == 62) {
                                input_position = input_position + 1;
                                if (statement[input_position] == 61) {
                                    comparison = 5;
                                    input_position = input_position + 1;
                                } else {
                                    comparison = 3;
                                }
                            }
                            if (comparison == 0) {
                                valid = 0;
                            } else {
                                expression_phase = 1;
                                expression_position = input_position;
                                expression_valid = 1;
                                expect_operand = 1;
                                value_count = 0;
                                operator_count = 0;
                                input_position = expression_position;
                                expression_mode = 4;
                            }
                        } else if (expression_mode == 4) {
                            right_value = expression_value;
                            while (statement[input_position] == 32 ||
                                statement[input_position] == 9) {
                                input_position = input_position + 1;
                            }
                            if (statement[input_position] != 116 ||
                                statement[input_position + 1] != 104 ||
                                statement[input_position + 2] != 101 ||
                                statement[input_position + 3] != 110 ||
                                (statement[input_position + 4] != 0 &&
                                    statement[input_position + 4] != 32 &&
                                    statement[input_position + 4] != 9)) {
                                valid = 0;
                            } else {
                                input_position = input_position + 4;
                                while (statement[input_position] == 32 ||
                                    statement[input_position] == 9) {
                                    input_position = input_position + 1;
                                }
                                target_number = 0;
                                target_valid = 0;
                                character = statement[input_position];
                                while (character >= 48 && character <= 57) {
                                    target_valid = 1;
                                    digit = character - 48;
                                    if (target_number > (9999 - digit) / 10) {
                                        valid = 0;
                                    } else {
                                        target_number =
                                            target_number * 10 + digit;
                                    }
                                    input_position = input_position + 1;
                                    character = statement[input_position];
                                }
                                while (character == 32 || character == 9) {
                                    input_position = input_position + 1;
                                    character = statement[input_position];
                                }
                                if (target_valid == 0 || character != 0) {
                                    valid = 0;
                                } else {
                                    if (comparison == 1) {
                                        condition = left_value == right_value;
                                    } else if (comparison == 2) {
                                        condition = left_value < right_value;
                                    } else if (comparison == 3) {
                                        condition = left_value > right_value;
                                    } else if (comparison == 4) {
                                        condition = left_value <= right_value;
                                    } else if (comparison == 5) {
                                        condition = left_value >= right_value;
                                    } else {
                                        condition = left_value != right_value;
                                    }
                                    if (condition != 0) {
                                        found = 0;
                                        line_index = 0;
                                        while (line_index < line_count) {
                                            if (line_numbers[line_index] ==
                                                target_number) {
                                                next_line = line_index;
                                                found = 1;
                                            }
                                            line_index = line_index + 1;
                                        }
                                        if (found == 0) {
                                            printf("BASIC: line not found.\n");
                                            halted = 1;
                                        }
                                    }
                                    expression_mode = 0;
                                }
                            }
                        }
                    }

                    if (valid == 0) {
                        if (execution_mode == 1) {
                            printf("BASIC: error in line ");
                            printf("%d", line_numbers[current_line]);
                            printf(".\n");
                        } else {
                            printf("BASIC: invalid command.\n");
                        }
                        execution_mode = 0;
                    } else if (halted != 0) {
                        execution_mode = 0;
                    } else {
                        if (execution_mode == 1) {
                            line_index = next_line;
                        } else {
                            execution_mode = 0;
                        }
                    }
                }
            }
        }
    }
    return 0;
}
