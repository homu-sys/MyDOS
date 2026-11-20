#include <mydos.h>
#include <kernel/system/identity.h>
#include <shell/env.h>
#include <shell/prompt.h>

static char shell_path[SHELL_PATH_MAX] = ".:/BIN:/USR/BIN";

const char *shell_env_get_path(void) {
    return shell_path;
}

void shell_env_set_path(const char *path) {
    uint32_t position = 0;
    if (path == 0) {
        path = "";
    }
    while (*path != '\0' && position + 1 < sizeof(shell_path)) {
        shell_path[position++] = *path++;
    }
    shell_path[position] = '\0';
}

const char *shell_env_get(const char *name) {
    if (name == 0) {
        return "";
    }
    if (strcmp(name, "PATH") == 0) {
        return shell_path;
    }
    if (strcmp(name, "USER") == 0) {
        return identity_username();
    }
    if (strcmp(name, "HOSTNAME") == 0 || strcmp(name, "HOST") == 0) {
        return identity_hostname();
    }
    if (strcmp(name, "PS1") == 0) {
        return shell_get_ps1();
    }
    return "";
}
