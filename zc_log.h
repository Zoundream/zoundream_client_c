#ifndef __ZC_LOG__
#define __ZC_LOG__

/* Small logging indirection so the same client code can print to stdout (command line
 * client) or feed a GUI log pane. Every call to zc_log() produces exactly one log line
 * (no trailing newline in the format string).
 *
 * By default, when no sink is set, lines are printed to stdout.
 */

typedef void (*ZcLogSink)(void* ctx, const char* line);

void zc_set_log_sink(ZcLogSink sink, void* ctx);
void zc_log(const char* format, ...) __attribute__((format(printf, 1, 2)));

#endif
