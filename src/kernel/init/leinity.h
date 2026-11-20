#ifndef MYDOS_LEINITY_H
#define MYDOS_LEINITY_H

#include <mydos.h>

int leinity_boot(FRESULT fs_result);
void leinity_status(int *cursor_y);
void leinity_reload(int *cursor_y);

#endif
