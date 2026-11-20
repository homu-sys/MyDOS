#include <mydos.h>
#include <mbedtls/entropy.h>
#include <mbedtls/platform.h>

static unsigned int mydos_tls_entropy_mix = 0x9e3779b9u;

static void *mydos_tls_calloc(size_t count, size_t size) {
    if (size != 0 && count > 0xffffffffu / size) {
        return 0;
    }
    unsigned int total = (unsigned int)(count * size);
    void *pointer = mydos_zstd_malloc(total);
    if (pointer != 0) {
        memset(pointer, 0, total);
    }
    return pointer;
}

static void mydos_tls_free(void *pointer) {
    (void)pointer;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t length,
                          size_t *olen) {
    (void)data;
    for (size_t i = 0; i < length; i++) {
        mydos_tls_entropy_mix ^= (unsigned int)rtc_read(0x00);
        mydos_tls_entropy_mix ^= (unsigned int)rtc_read(0x02) << 8;
        mydos_tls_entropy_mix ^= (unsigned int)rtc_read(0x04) << 16;
        mydos_tls_entropy_mix ^= (unsigned int)rtc_read(0x07) << 24;
        mydos_tls_entropy_mix ^= mydos_tls_entropy_mix << 13;
        mydos_tls_entropy_mix ^= mydos_tls_entropy_mix >> 17;
        mydos_tls_entropy_mix ^= mydos_tls_entropy_mix << 5;
        output[i] = (unsigned char)(mydos_tls_entropy_mix >> 24);
    }
    *olen = length;
    return 0;
}

void mydos_tls_platform_init(void) {
    mbedtls_platform_set_calloc_free(mydos_tls_calloc, mydos_tls_free);
}
