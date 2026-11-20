#include <cmd/cmd.h>
#include <kernel/system/identity.h>

static void identity_command(const char *label, char *args, int *cursor_y,
                             const char *current,
                             void (*setter)(const char *)) {
    char *name = cmd_next_token(&args);
    if (name != 0) {
        setter(name);
        print(label, LIGHT_CYAN, 0, *cursor_y);
        print(": ", WHITE, strlen(label), *cursor_y);
        print(name, GREEN, strlen(label) + 2, *cursor_y);
    } else {
        print(label, LIGHT_CYAN, 0, *cursor_y);
        print(": ", WHITE, strlen(label), *cursor_y);
        print(current, GREEN, strlen(label) + 2, *cursor_y);
    }
    scrl(cursor_y);
}

void cmd_hostname(char *args, int *cursor_y) {
    identity_command("hostname", args, cursor_y, identity_hostname(),
                     identity_set_hostname);
}

void cmd_user(char *args, int *cursor_y) {
    identity_command("user", args, cursor_y, identity_username(),
                     identity_set_username);
}

void cmd_whoami(char *args, int *cursor_y) {
    (void)args;
    print(identity_username(), GREEN, 0, *cursor_y);
    scrl(cursor_y);
}
