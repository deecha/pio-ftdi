#include <stddef.h>

// --- Standard String & Memory Functions ---

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return p - s;
}

// --- ARM EABI Math Function Stubs ---

// Unsigned 64-bit division/modulo helper
unsigned long long __aeabi_uldivmod(unsigned long long numerator, unsigned long long denominator, unsigned long long *remainder) {
    unsigned long long res = 0;
    unsigned long long curr = 1;
    if (denominator == 0) return 0;
    while (denominator < numerator && (denominator & (1ULL << 63)) == 0) {
        denominator <<= 1;
        curr <<= 1;
    }
    while (curr > 0) {
        if (numerator >= denominator) {
            numerator -= denominator;
            res |= curr;
        }
        denominator >>= 1;
        curr >>= 1;
    }
    if (remainder) *remainder = numerator;
    return res;
}

// Type conversion helpers (unsigned integer to float, float to unsigned integer)
float __aeabi_ui2f(unsigned int bits) { return (float)bits; }
unsigned int __aeabi_f2uiz(float f)   { return (unsigned int)f; }

// Floating-point math primitives
float __aeabi_fadd(float a, float b)  { return a + b; }
float __aeabi_fsub(float a, float b)  { return a - b; }
float __aeabi_fmul(float a, float b)  { return a * b; }
float __aeabi_fdiv(float a, float b)  { return a / b; }

// Floating-point comparison functions (ARM EABI standards)
int __aeabi_fcmplt(float a, float b)  { return (a < b) ? 1 : 0; }
int __aeabi_fcmpgt(float a, float b)  { return (a > b) ? 1 : 0; }

