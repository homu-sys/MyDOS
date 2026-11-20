#include <mydos.h>

int strlen(const char* s) {
    int len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }

    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, int n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }

    if (n == 0) return 0;

    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void* memset(void* destination, int value, unsigned int count) {
    unsigned char* bytes = (unsigned char*)destination;
    while (count-- != 0u) *bytes++ = (unsigned char)value;
    return destination;
}

void* memcpy(void* destination, const void* source, unsigned int count) {
    unsigned char* output = (unsigned char*)destination;
    const unsigned char* input = (const unsigned char*)source;
    while (count-- != 0u) *output++ = *input++;
    return destination;
}

int memcmp(const void* first, const void* second, unsigned int count) {
    const unsigned char* left = (const unsigned char*)first;
    const unsigned char* right = (const unsigned char*)second;
    while (count-- != 0u) {
        if (*left != *right) return (int)*left - (int)*right;
        left++; right++;
    }
    return 0;
}

char* strchr(const char* string, int character) {
    while (*string != '\0') {
        if (*string == (char)character) return (char*)string;
        string++;
    }
    return character == '\0' ? (char*)string : 0;
}

char* strstr(const char* haystack, const char* needle) {
    if (*needle == '\0') return (char*)haystack;
    while (*haystack != '\0') {
        const char* current = haystack;
        const char* search = needle;
        while (*current != '\0' && *search != '\0' && *current == *search) {
            current++;
            search++;
        }
        if (*search == '\0') return (char*)haystack;
        haystack++;
    }
    return 0;
}

void explicit_bzero(void* destination, unsigned int count) {
    memset(destination, 0, count);
}

int inet_pton(int family, const char* text, void* output) {
    unsigned char* bytes = (unsigned char*)output;
    unsigned int value = 0;
    int digits = 0;
    int part = 0;
    if (family != 2) return 0;
    while (*text != '\0' && part < 4) {
        if (*text >= '0' && *text <= '9') {
            value = value * 10u + (unsigned int)(*text - '0');
            digits++;
            if (digits > 3 || value > 255u) return 0;
        } else if (*text == '.' && digits > 0 && part < 3) {
            bytes[part++] = (unsigned char)value;
            value = 0;
            digits = 0;
        } else {
            return 0;
        }
        text++;
    }
    if (part != 3 || digits == 0) return 0;
    bytes[3] = (unsigned char)value;
    return 1;
}

void itoanum(unsigned int value, char out[32]) {
    char tmp[32];
    int i = 0;

    if (value == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }

    while (value > 0) {
        tmp[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    for (int j = 0; j < i; j++) {
        out[j] = tmp[i - 1 - j];
    }

    out[i] = '\0';
}

void itohex(unsigned int value, char out[11]) {
    static const char hex_digits[] = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 8; i++) {
        int shift = (7 - i) * 4;
        out[2 + i] = hex_digits[(value >> shift) & 0xF];
    }
    out[10] = '\0';
}
