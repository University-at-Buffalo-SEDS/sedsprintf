// Minimal stub to satisfy link-time references when wide formatting isn't used.
// Keep this in C and avoid pulling wchar.h to sidestep prototype clashes.
#include <stddef.h>
typedef __WCHAR_TYPE__ wchar_t;   // matches the toolchain's wchar_t

int swprintf(wchar_t *s, size_t n, const wchar_t *fmt, ...) {
    (void)n; (void)fmt;
    if (s) s[0] = 0;
    return -1; // indicate failure if ever called
}
