#include <mydos.h>
#include "identity.h"

static char system_hostname[IDENTITY_NAME_MAX] = "mydos";
static char system_username[IDENTITY_NAME_MAX] = "root";

static void identity_copy(char *out, const char *name) {
    char sanitized[IDENTITY_NAME_MAX];
    unsigned int length = 0;

    if (name == 0) {
        return;
    }
    while (*name != '\0' && length + 1 < sizeof(sanitized)) {
        unsigned char character = (unsigned char)*name++;
        int allowed = (character >= 'a' && character <= 'z') ||
                      (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') ||
                      character == '-' || character == '_' || character == '.';
        if (allowed) {
            sanitized[length++] = (char)character;
        }
    }
    sanitized[length] = '\0';
    if (length == 0) {
        return;
    }
    for (unsigned int i = 0; i < sizeof(sanitized); i++) {
        out[i] = sanitized[i];
        if (sanitized[i] == '\0') {
            break;
        }
    }
}

void identity_init(void) {
    identity_copy(system_hostname, "mydos");
    identity_copy(system_username, "root");
}

const char *identity_hostname(void) {
    return system_hostname;
}

const char *identity_username(void) {
    return system_username;
}

void identity_set_hostname(const char *name) {
    identity_copy(system_hostname, name);
}

void identity_set_username(const char *name) {
    identity_copy(system_username, name);
}
