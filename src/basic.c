#include "kernel.h"

#define BASIC_PROGRAM_LINE_COUNT 16
#define BASIC_VALUE_LIMIT 32767

typedef struct
{
    unsigned int number;
    char text[COMMAND_BUFFER_SIZE];
} BasicProgramLine;

typedef struct
{
    const char *cursor;
    int valid;
} BasicExpression;

static BasicProgramLine basic_program[BASIC_PROGRAM_LINE_COUNT];
static unsigned int basic_program_count;
static int basic_variables[26];

static void basic_skip_spaces(const char **cursor)
{
    while (is_space(**cursor))
    {
        ++*cursor;
    }
}

static char basic_uppercase(char character)
{
    if (character >= 'a' && character <= 'z')
    {
        return (char)(character - 'a' + 'A');
    }
    return character;
}

static int basic_match_keyword(const char **cursor, const char *keyword)
{
    const char *start = *cursor;
    const char *word = keyword;

    basic_skip_spaces(&start);
    while (*word != '\0' &&
        basic_uppercase(*start) == *word)
    {
        ++start;
        ++word;
    }

    if (*word != '\0' ||
        (basic_uppercase(*start) >= 'A' &&
            basic_uppercase(*start) <= 'Z') ||
        (*start >= '0' && *start <= '9'))
    {
        return 0;
    }

    *cursor = start;
    return 1;
}

static int basic_parse_expression(BasicExpression *expression);

static int basic_parse_factor(BasicExpression *expression)
{
    int value;
    int negative = 0;
    char character;

    basic_skip_spaces(&expression->cursor);
    if (*expression->cursor == '+' || *expression->cursor == '-')
    {
        negative = *expression->cursor == '-';
        ++expression->cursor;
        value = basic_parse_factor(expression);
        return negative ? -value : value;
    }

    character = basic_uppercase(*expression->cursor);
    if (character == '(')
    {
        ++expression->cursor;
        value = basic_parse_expression(expression);
        basic_skip_spaces(&expression->cursor);
        if (*expression->cursor != ')')
        {
            expression->valid = 0;
            return 0;
        }
        ++expression->cursor;
        return value;
    }

    if (character >= 'A' && character <= 'Z')
    {
        ++expression->cursor;
        return basic_variables[character - 'A'];
    }

    if (*expression->cursor >= '0' && *expression->cursor <= '9')
    {
        value = 0;
        while (*expression->cursor >= '0' &&
            *expression->cursor <= '9')
        {
            int digit = *expression->cursor - '0';
            if (value > (BASIC_VALUE_LIMIT - digit) / 10)
            {
                expression->valid = 0;
                return 0;
            }
            value = value * 10 + digit;
            ++expression->cursor;
        }
        return value;
    }

    expression->valid = 0;
    return 0;
}

static int basic_parse_term(BasicExpression *expression)
{
    int value = basic_parse_factor(expression);

    for (;;)
    {
        char operation;
        int right;

        basic_skip_spaces(&expression->cursor);
        operation = *expression->cursor;
        if (operation != '*' && operation != '/')
        {
            return value;
        }

        ++expression->cursor;
        right = basic_parse_factor(expression);
        if (!expression->valid)
        {
            return 0;
        }
        if (operation == '/')
        {
            if (right == 0)
            {
                expression->valid = 0;
                return 0;
            }
            value /= right;
        }
        else
        {
            value *= right;
        }
        if (value > BASIC_VALUE_LIMIT || value < -BASIC_VALUE_LIMIT)
        {
            expression->valid = 0;
            return 0;
        }
    }
}

static int basic_parse_expression(BasicExpression *expression)
{
    int value = basic_parse_term(expression);

    for (;;)
    {
        char operation;
        int right;

        basic_skip_spaces(&expression->cursor);
        operation = *expression->cursor;
        if (operation != '+' && operation != '-')
        {
            return value;
        }

        ++expression->cursor;
        right = basic_parse_term(expression);
        if (!expression->valid)
        {
            return 0;
        }
        value = operation == '+' ? value + right : value - right;
        if (value > BASIC_VALUE_LIMIT || value < -BASIC_VALUE_LIMIT)
        {
            expression->valid = 0;
            return 0;
        }
    }
}

static int basic_expression_is_complete(BasicExpression *expression)
{
    basic_skip_spaces(&expression->cursor);
    return expression->valid && *expression->cursor == '\0';
}

static int basic_find_line(unsigned int number)
{
    unsigned int index;

    for (index = 0; index < basic_program_count; ++index)
    {
        if (basic_program[index].number == number)
        {
            return (int)index;
        }
    }
    return -1;
}

static void basic_write_value(int value)
{
    if (value < 0)
    {
        terminal_write_character('-');
        value = -value;
    }
    terminal_write_unsigned((unsigned int)value);
}

static int basic_parse_line_target(
    const char **cursor,
    unsigned int *target)
{
    unsigned int value = 0;

    basic_skip_spaces(cursor);
    if (**cursor < '0' || **cursor > '9')
    {
        return 0;
    }

    while (**cursor >= '0' && **cursor <= '9')
    {
        unsigned int digit = (unsigned int)(**cursor - '0');
        if (value > (9999 - digit) / 10)
        {
            return 0;
        }
        value = value * 10 + digit;
        ++*cursor;
    }

    *target = value;
    return 1;
}

static int basic_execute_statement(
    const char *statement,
    int current_line,
    int *next_line,
    int *halted)
{
    const char *cursor = statement;
    BasicExpression expression;
    int value;

    *next_line = current_line + 1;
    *halted = 0;

    if (basic_match_keyword(&cursor, "REM"))
    {
        return 1;
    }

    if (basic_match_keyword(&cursor, "PRINT"))
    {
        basic_skip_spaces(&cursor);
        if (*cursor == '"')
        {
            ++cursor;
            while (*cursor != '\0' && *cursor != '"')
            {
                terminal_write_character(*cursor++);
            }
            if (*cursor != '"')
            {
                return 0;
            }
            ++cursor;
            basic_skip_spaces(&cursor);
            if (*cursor != '\0')
            {
                return 0;
            }
            terminal_write("\n");
            return 1;
        }

        expression.cursor = cursor;
        expression.valid = 1;
        value = basic_parse_expression(&expression);
        if (!basic_expression_is_complete(&expression))
        {
            return 0;
        }
        basic_write_value(value);
        terminal_write("\n");
        return 1;
    }

    if (basic_match_keyword(&cursor, "LET"))
    {
        char variable;

        basic_skip_spaces(&cursor);
        variable = basic_uppercase(*cursor);
        if (variable < 'A' || variable > 'Z')
        {
            return 0;
        }
        ++cursor;
        basic_skip_spaces(&cursor);
        if (*cursor != '=')
        {
            return 0;
        }
        expression.cursor = cursor + 1;
        expression.valid = 1;
        value = basic_parse_expression(&expression);
        if (!basic_expression_is_complete(&expression))
        {
            return 0;
        }
        basic_variables[variable - 'A'] = value;
        return 1;
    }

    if (basic_match_keyword(&cursor, "GOTO"))
    {
        unsigned int target;
        int destination;

        if (current_line < 0 ||
            !basic_parse_line_target(&cursor, &target))
        {
            return 0;
        }
        basic_skip_spaces(&cursor);
        if (*cursor != '\0')
        {
            return 0;
        }
        destination = basic_find_line(target);
        if (destination < 0)
        {
            terminal_write("BASIC: line not found.\n");
            *halted = 1;
            return 1;
        }
        *next_line = destination;
        return 1;
    }

    if (basic_match_keyword(&cursor, "IF"))
    {
        int left;
        int right;
        int condition;
        char comparison;

        expression.cursor = cursor;
        expression.valid = 1;
        left = basic_parse_expression(&expression);
        if (!expression.valid)
        {
            return 0;
        }
        basic_skip_spaces(&expression.cursor);
        comparison = *expression.cursor++;
        if (comparison == '<' && *expression.cursor == '>')
        {
            ++expression.cursor;
            comparison = '!';
        }
        else if ((comparison == '<' || comparison == '>') &&
            *expression.cursor == '=')
        {
            ++expression.cursor;
            comparison = comparison == '<' ? 'l' : 'g';
        }
        else if (comparison != '<' && comparison != '>' &&
            comparison != '=')
        {
            return 0;
        }

        right = basic_parse_expression(&expression);
        if (!expression.valid ||
            !basic_match_keyword(&expression.cursor, "THEN"))
        {
            return 0;
        }
        condition = comparison == '=' ? left == right :
            comparison == '<' ? left < right :
            comparison == '>' ? left > right :
            comparison == 'l' ? left <= right :
            comparison == 'g' ? left >= right :
            left != right;

        {
            unsigned int target;
            int target_valid = basic_parse_line_target(
                &expression.cursor, &target);

            basic_skip_spaces(&expression.cursor);
            if (!target_valid || *expression.cursor != '\0')
            {
                return 0;
            }

            if (condition)
            {
                int destination;

                if (current_line < 0)
                {
                    return 0;
                }
                destination = basic_find_line(target);
                if (destination < 0)
                {
                    terminal_write("BASIC: line not found.\n");
                    *halted = 1;
                    return 1;
                }
                *next_line = destination;
            }
        }
        return 1;
    }

    if (basic_match_keyword(&cursor, "END") ||
        basic_match_keyword(&cursor, "STOP"))
    {
        basic_skip_spaces(&cursor);
        if (*cursor != '\0')
        {
            return 0;
        }
        *halted = 1;
        return 1;
    }

    return 0;
}

static int basic_store_line(const char *input)
{
    const char *cursor = input;
    unsigned int number = 0;
    unsigned int index;
    int existing;

    if (*cursor < '0' || *cursor > '9')
    {
        return 0;
    }
    while (*cursor >= '0' && *cursor <= '9')
    {
        unsigned int digit = (unsigned int)(*cursor - '0');
        if (number > (9999 - digit) / 10)
        {
            return 0;
        }
        number = number * 10 + digit;
        ++cursor;
    }
    if (number == 0 || (*cursor != '\0' && !is_space(*cursor)))
    {
        return 0;
    }

    basic_skip_spaces(&cursor);
    existing = basic_find_line(number);
    if (*cursor == '\0')
    {
        if (existing >= 0)
        {
            for (index = (unsigned int)existing;
                index + 1 < basic_program_count; ++index)
            {
                basic_program[index] = basic_program[index + 1];
            }
            --basic_program_count;
        }
        return 1;
    }

    if (existing < 0)
    {
        if (basic_program_count == BASIC_PROGRAM_LINE_COUNT)
        {
            terminal_write("BASIC: program is full.\n");
            return 1;
        }
        index = 0;
        while (index < basic_program_count &&
            basic_program[index].number < number)
        {
            ++index;
        }
        for (existing = (int)basic_program_count;
            existing > (int)index; --existing)
        {
            basic_program[existing] = basic_program[existing - 1];
        }
        ++basic_program_count;
    }
    else
    {
        index = (unsigned int)existing;
    }

    basic_program[index].number = number;
    for (existing = 0; existing < COMMAND_BUFFER_SIZE - 1 &&
        cursor[existing] != '\0'; ++existing)
    {
        basic_program[index].text[existing] = cursor[existing];
    }
    basic_program[index].text[existing] = '\0';
    if (cursor[existing] != '\0')
    {
        terminal_write("BASIC: line is too long.\n");
    }
    return 1;
}

static void basic_list_program(void)
{
    unsigned int index;

    for (index = 0; index < basic_program_count; ++index)
    {
        terminal_write_unsigned(basic_program[index].number);
        terminal_write_character(' ');
        terminal_write(basic_program[index].text);
        terminal_write("\n");
    }
}

static void basic_run_program(void)
{
    int line = 0;
    unsigned int steps = 0;
    int halted = 0;
    int index;

    for (index = 0; index < 26; ++index)
    {
        basic_variables[index] = 0;
    }

    while (line >= 0 && (unsigned int)line < basic_program_count &&
        !halted)
    {
        int next_line;

        if (++steps > 10000)
        {
            terminal_write("BASIC: execution limit reached.\n");
            return;
        }
        if (!basic_execute_statement(
            basic_program[line].text, line, &next_line, &halted))
        {
            terminal_write("BASIC: error in line ");
            terminal_write_unsigned(basic_program[line].number);
            terminal_write(".\n");
            return;
        }
        line = next_line;
    }
}

static void basic_read_line(
    char *buffer,
    const char *prompt,
    unsigned char *shift_pressed,
    unsigned char *caps_lock,
    unsigned char *extended)
{
    unsigned int length = 0;
    char character;

    terminal_write(prompt);
    for (;;)
    {
        do
        {
            character = keyboard_decode_scancode(
                process_keyboard_read_scancode(),
                shift_pressed,
                caps_lock,
                extended);
        } while (character == '\0');
        if (character == '\n')
        {
            terminal_write("\n");
            buffer[length] = '\0';
            return;
        }
        if (character == '\b')
        {
            if (length != 0)
            {
                buffer[--length] = '\0';
                terminal_write_character('\b');
            }
            continue;
        }
        if (character >= ' ' && character <= '~' &&
            length < COMMAND_BUFFER_SIZE - 1)
        {
            buffer[length++] = character;
            terminal_write_character(character);
        }
    }
}

void basic_repl(void)
{
    char input[COMMAND_BUFFER_SIZE];
    unsigned char shift_pressed = 0;
    unsigned char caps_lock = 0;
    unsigned char extended = 0;

    /* The system-call interrupt gate disables IRQs; BASIC waits on keyboard IRQs. */
    __asm sti

    for (;;)
    {
        const char *cursor;
        basic_read_line(
            input, "basic> ", &shift_pressed, &caps_lock, &extended);
        cursor = input;
        basic_skip_spaces(&cursor);
        if (*cursor == '\0')
        {
            continue;
        }

        if (basic_store_line(cursor))
        {
            continue;
        }

        cursor = input;
        if (basic_match_keyword(&cursor, "RUN") && *cursor == '\0')
        {
            basic_run_program();
        }
        else if (basic_match_keyword(&cursor, "LIST") && *cursor == '\0')
        {
            basic_list_program();
        }
        else if (basic_match_keyword(&cursor, "NEW") && *cursor == '\0')
        {
            basic_program_count = 0;
            terminal_write("BASIC: program cleared.\n");
        }
        else if ((basic_match_keyword(&cursor, "EXIT") ||
            basic_match_keyword(&cursor, "QUIT")) && *cursor == '\0')
        {
            return;
        }
        else
        {
            int next_line;
            int halted;

            if (!basic_execute_statement(input, -1, &next_line, &halted))
            {
                terminal_write("BASIC: invalid command.\n");
            }
        }
    }
}
