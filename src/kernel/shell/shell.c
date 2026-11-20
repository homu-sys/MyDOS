#include <mydos.h>
#include <cmd/cmd.h>
#include <shell/external.h>
#include <shell/prompt.h>

static int active_cursor_y;

int shell_get_cursor_y(void) {
    return active_cursor_y;
}

void shell_set_cursor_y(int cursor_y) {
    active_cursor_y = cursor_y;
}

int shell_execute_line(const char *line, int *cursor_y) {
    active_cursor_y = *cursor_y;

    char command[VGA_MAX_WIDTH + 1];
    unsigned int length = 0;

    while (line != 0 && *line != '\0' && length < sizeof(command) - 1) {
        command[length++] = *line++;
    }
    command[length] = '\0';

    char *name = command;
    while (*name == ' ') {
        name++;
    }
    if (*name == '\0') {
        return 0;
    }

    char *args = "";
    for (unsigned int i = 0; name[i] != '\0'; i++) {
        if (name[i] == ' ') {
            name[i] = '\0';
            args = &name[i + 1];
            break;
        }
    }

    int executed = cmd_run(name, args, cursor_y) ||
                   shell_run_external(name, args, cursor_y);
    if (!executed) {
        print("shell: unknown command, try again or run 'help' for assistance",
              RED, 0, *cursor_y);
        scrl(cursor_y);
    }

    active_cursor_y = *cursor_y;
    return executed;
}

void shell_run(unsigned int mbi, int cursor_y) {
    active_cursor_y = cursor_y;
    int *cursor = &active_cursor_y;
    int cursor_x = 0;
    int prompt_width = shell_prompt_render(*cursor, &cursor_x);
    int max_cmd_len = vga_screen_width() - prompt_width - 1;
    if (max_cmd_len > VGA_MAX_WIDTH)
        max_cmd_len = VGA_MAX_WIDTH;
    if (max_cmd_len < 0)
        max_cmd_len = 0;
    move_cursor(cursor_x, *cursor);

    char cmd[VGA_MAX_WIDTH + 1];
    int cmd_len = 0;

    cmd_set_mbi(mbi);
    cmd_uptime_init();

    print("Thank you for trying out MyDOS!", WHITE, 0, 7);
    print("If you found a bug or have a suggestion, message me on Discord! aviancaflight17", WHITE, 0, 8);

    while (1) {
        char c = keyboard_getchar();

        if (c == 0)
            continue;

        if (c == '\b') {
            if (cursor_x > prompt_width && cmd_len > 0) {
                cmd_len--;
                cmd[cmd_len] = '\0';

                cursor_x--;
                print(" ", WHITE, cursor_x, *cursor);
                move_cursor(cursor_x, *cursor);
            }
        }

        else if (c == '\n') {
            cmd[cmd_len] = '\0';

            scrl(cursor);

            if (cmd[0] != '\0') {
                shell_execute_line(cmd, cursor);
            }

            cursor_x = 0;
            prompt_width = shell_prompt_render(*cursor, &cursor_x);
            max_cmd_len = vga_screen_width() - prompt_width - 1;
            if (max_cmd_len > VGA_MAX_WIDTH)
                max_cmd_len = VGA_MAX_WIDTH;
            if (max_cmd_len < 0)
                max_cmd_len = 0;

            cmd_len = 0;
            move_cursor(cursor_x, *cursor);
        }

        else {
            if (cmd_len < max_cmd_len) {
                cmd[cmd_len++] = c;

                char str[2] = {c, '\0'};
                print(str, WHITE, cursor_x, *cursor);

                cursor_x++;
                move_cursor(cursor_x, *cursor);
            }
        }
    }
}
