#include "zc_io.h"

#ifdef _WIN32

#include <errno.h>
#include <stdlib.h>
#include <windows.h>

/** Converts a UTF-8 string to a newly allocated UTF-16 one (freed by the caller).
 *
 * @returns the converted string, or 0 if it could not be converted or allocated.
 */
static wchar_t* to_utf16(const char* utf8) {
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0); // includes the terminator
    if (length <= 0) return 0;

    wchar_t* wide = (wchar_t*) malloc((size_t) length * sizeof(wchar_t));
    if (wide == NULL) return 0;

    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, length) <= 0) {
        free(wide);
        return 0;
    }
    return wide;
}

FILE* zc_fopen(const char* path_utf8, const char* mode) {
    wchar_t* wide_path = to_utf16(path_utf8);
    wchar_t* wide_mode = to_utf16(mode);
    FILE* file = 0;

    if (wide_path != 0 && wide_mode != 0) {
        file = _wfopen(wide_path, wide_mode); // sets errno on failure, like fopen()
    } else {
        errno = EINVAL; // the path is not valid UTF-8, or we are out of memory
    }

    free(wide_path);
    free(wide_mode);
    return file;
}

#else

FILE* zc_fopen(const char* path_utf8, const char* mode) {
    return fopen(path_utf8, mode);
}

#endif
