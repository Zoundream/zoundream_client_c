#ifndef __ZC_IO__
#define __ZC_IO__

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opens a file whose path is UTF-8 encoded, on every platform.
 *
 * This exists because of Windows: there, the narrow fopen() decodes the path with the
 * process ANSI code page (GBK on a Chinese system, Latin-1 on a Western European one),
 * while every path we receive is UTF-8 (GLFW's drop callback and tinyfiledialogs both
 * hand out UTF-8). Any non-ASCII character in the path - a Chinese user name in
 * C:\Users\..., an accented folder name - would therefore be mangled and the file would
 * fail to open. So on Windows the path is converted to UTF-16 and _wfopen() is used,
 * which is encoding-independent. Elsewhere paths are just bytes and this is a plain fopen().
 *
 * @returns an open FILE*, or 0 on failure (errno is set as by fopen()).
 */
FILE* zc_fopen(const char* path_utf8, const char* mode);

#ifdef __cplusplus
}
#endif

#endif
