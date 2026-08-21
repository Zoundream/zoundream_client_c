#include "zc_log.h"

#include <stdio.h>
#include <stdarg.h>

static ZcLogSink log_sink = NULL;
static void* log_sink_ctx = NULL;

void zc_set_log_sink(ZcLogSink sink, void* ctx)
{
    log_sink = sink;
    log_sink_ctx = ctx;
}

void zc_log(const char* format, ...)
{
    char line[2048];

    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    if (log_sink) {
        log_sink(log_sink_ctx, line);
    } else {
        fputs(line, stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
}
