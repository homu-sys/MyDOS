#ifndef MYDOS_KERNEL_SYSTEM_IDENTITY_H
#define MYDOS_KERNEL_SYSTEM_IDENTITY_H

#define IDENTITY_NAME_MAX 32u

void identity_init(void);
const char *identity_hostname(void);
const char *identity_username(void);
void identity_set_hostname(const char *name);
void identity_set_username(const char *name);

#endif
