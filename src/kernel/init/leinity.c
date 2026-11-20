#include <init/leinity.h>

#define LEINITY_VERSION "1.0.0"
#define LEINITY_SERVICE_PATH "ETC/LEINITY/SERVICES.TXT"
#define LEINITY_MAX_SERVICES 16

static unsigned int leinity_boot_start_cs;
static unsigned int leinity_boot_duration_ms;
static int leinity_boot_fs_ok;
static int leinity_boot_services;

static unsigned int leinity_now_cs(void) {
    unsigned int hundredths = bcdtb(rtc_read(0x00));
    unsigned int seconds = bcdtb(rtc_read(0x02));
    unsigned int minutes = bcdtb(rtc_read(0x04));

    if (hundredths >= 100) hundredths = 0;
    if (seconds >= 60) seconds = 0;
    if (minutes >= 60) minutes = 0;
    return minutes * 60000u + seconds * 1000u + hundredths * 10u;
}

static unsigned int leinity_elapsed_ms(void) {
    unsigned int now = leinity_now_cs();
    if (now < leinity_boot_start_cs) now += 3600000u;
    return (now - leinity_boot_start_cs) / 10u;
}

static void le_uint_padded(unsigned int value, char *out, unsigned int width) {
    char digits[12];
    unsigned int length = 0;
    unsigned int index = 0;

    if (value == 0) {
        digits[length++] = '0';
    } else {
        while (value != 0 && length < sizeof(digits)) {
            digits[length++] = (char)('0' + value % 10);
            value /= 10;
        }
    }

    unsigned int padding = length < width ? width - length : 0;
    while (index < padding) {
        out[index++] = ' ';
    }
    while (length > 0) {
        out[index++] = digits[--length];
    }
    out[index] = '\0';
}

static void leinity_heading(int *cursor_y) {
    print("+--------------------------------------------------------------------------+", BLUE, 0, *cursor_y);
    scrl(cursor_y);
    print("  LEINITY " LEINITY_VERSION "  -  FAST BOOT SUPERVISOR", LIGHT_CYAN, 0, *cursor_y);
    scrl(cursor_y);
    print("+--------------------------------------------------------------------------+", BLUE, 0, *cursor_y);
    scrl(cursor_y);
}

static void leinity_log(const char *tag, unsigned char tag_color,
                        const char *label, int *cursor_y) {
    char elapsed[12];
    le_uint_padded(leinity_elapsed_ms(), elapsed, 5);

    print("[", DARK_GREY, 0, *cursor_y);
    print(elapsed, LIGHT_GREY, 1, *cursor_y);
    print("ms]", DARK_GREY, 6, *cursor_y);
    print(tag, tag_color, 11, *cursor_y);
    print(label, WHITE, 17, *cursor_y);
    scrl(cursor_y);
}

static void leinity_ok(const char *label, int *cursor_y) {
    leinity_log("OK", LIGHT_GREEN, label, cursor_y);
}

static void leinity_warn(const char *label, int *cursor_y) {
    leinity_log("WARN", YELLOW, label, cursor_y);
}

static void leinity_fail(const char *label, int *cursor_y) {
    leinity_log("FAIL", LIGHT_RED, label, cursor_y);
}

static void leinity_trim(char *line) {
    unsigned int length = 0;
    while (line[length] != '\0') length++;

    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' ||
                          line[length - 1] == ' ')) {
        line[--length] = '\0';
    }

    unsigned int start = 0;
    while (line[start] == ' ' || line[start] == '\t') start++;
    if (start == 0) return;

    unsigned int index = 0;
    while (line[start] != '\0') line[index++] = line[start++];
    line[index] = '\0';
}

static int leinity_read_line(FIL *file, char *line, unsigned int capacity) {
    unsigned int position = 0;
    line[0] = '\0';

    for (;;) {
        char character = '\0';
        UINT read = 0;
        if (f_read(file, &character, 1, &read) != FR_OK || read == 0) {
            return position != 0;
        }
        if (character == '\n') {
            line[position] = '\0';
            return 1;
        }
        if (character == '\r') continue;
        if (position + 1 < capacity) {
            line[position++] = character;
            line[position] = '\0';
        }
    }
}

static int leinity_run_services(int *cursor_y, int quiet) {
    FIL file;
    char line[160];
    int count = 0;

    if (f_open(&file, LEINITY_SERVICE_PATH, FA_READ) != FR_OK) {
        if (!quiet) leinity_warn("no service list at ETC/LEINITY/SERVICES.TXT", cursor_y);
        return 0;
    }

    while (leinity_read_line(&file, line, sizeof(line))) {
        leinity_trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') continue;
        if (count >= LEINITY_MAX_SERVICES) {
            leinity_warn("service limit reached", cursor_y);
            break;
        }

        shell_execute_line(line, cursor_y);
        count++;
        if (!quiet) {
            char label[128];
            unsigned int index = 0;
            label[0] = '\0';
            while (line[index] != '\0' && index + 6 < sizeof(label)) {
                label[index] = line[index];
                index++;
            }
            label[index] = '\0';
            leinity_log("RUN", LIGHT_MAGENTA, label, cursor_y);
        }
    }

    f_close(&file);
    return count;
}

int leinity_boot(FRESULT fs_result) {
    int cursor_y = 9;
    leinity_boot_start_cs = leinity_now_cs();
    leinity_boot_duration_ms = 0;
    leinity_boot_fs_ok = fs_result == FR_OK;
    leinity_boot_services = 0;

    leinity_heading(&cursor_y);
    leinity_ok("core: identity, gdt, idt, fpu", &cursor_y);
    leinity_ok("input: keyboard layout de", &cursor_y);
    leinity_ok("video: text console ready", &cursor_y);

    if (leinity_boot_fs_ok) {
        leinity_ok("storage: fat filesystem mounted", &cursor_y);
        leinity_boot_services = leinity_run_services(&cursor_y, 0);
        if (leinity_boot_services == 0) {
            leinity_warn("services: none configured", &cursor_y);
        }
    } else {
        leinity_fail("storage: filesystem unavailable", &cursor_y);
    }

    leinity_ok("package runtime: gpkg and flint ready", &cursor_y);
    leinity_boot_duration_ms = leinity_elapsed_ms();

    char ready[96];
    unsigned int index = 0;
    const char *prefix = "handoff: shell starting in ";
    while (prefix[index] != '\0') {
        ready[index] = prefix[index];
        index++;
    }
    le_uint_padded(leinity_boot_duration_ms, ready + index, 1);
    index += strlen(ready + index);
    ready[index++] = 'm';
    ready[index++] = 's';
    ready[index] = '\0';
    leinity_ok(ready, &cursor_y);
    print("+--------------------------------------------------------------------------+", BLUE, 0, cursor_y);
    scrl(&cursor_y);
    return cursor_y;
}

void leinity_status(int *cursor_y) {
    char ready[64];
    char services[32];

    le_uint_padded(leinity_boot_duration_ms, ready, 1);
    le_uint_padded((unsigned int)leinity_boot_services, services, 1);

    leinity_heading(cursor_y);
    leinity_ok("status: leinity supervisor online", cursor_y);
    leinity_log(leinity_boot_fs_ok ? "OK" : "FAIL",
                leinity_boot_fs_ok ? LIGHT_GREEN : LIGHT_RED,
                leinity_boot_fs_ok ? "storage: online" : "storage: offline", cursor_y);

    print("  boot time: ", LIGHT_GREY, 0, *cursor_y);
    print(ready, WHITE, 13, *cursor_y);
    print("ms", WHITE, 13 + (int)strlen(ready), *cursor_y);
    scrl(cursor_y);

    print("  services: ", LIGHT_GREY, 0, *cursor_y);
    print(services, WHITE, 12, *cursor_y);
    scrl(cursor_y);

    print("  config: ", LIGHT_GREY, 0, *cursor_y);
    print(LEINITY_SERVICE_PATH, WHITE, 10, *cursor_y);
    scrl(cursor_y);
}

void leinity_reload(int *cursor_y) {
    leinity_heading(cursor_y);
    leinity_boot_services = leinity_run_services(cursor_y, 0);
    leinity_log("OK", LIGHT_GREEN, "service list reloaded", cursor_y);
}

void cmd_leinity(char *args, int *cursor_y) {
    if (strcmp(args, "reload") == 0) {
        leinity_reload(cursor_y);
        return;
    }
    leinity_status(cursor_y);
}
