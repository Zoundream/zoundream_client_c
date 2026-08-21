#ifndef __CLIENT_CORE__
#define __CLIENT_CORE__

#include "api.h"

/* The client session logic (read audio, send it to the API, decide when to loop the file
 * and when to stop), extracted so that both the command line client and the GUI run
 * exactly the same code.
 */

#define DEFAULT_MAX_LOOPS 3       // max number of times to loop back to the start looking for a valid translation, to avoid looping forever
#define MIN_AUDIO_SECONDS 10      // files shorter than this are rejected before anything is sent
#define MAX_REQUEST_FAILURES 5    // consecutive failed requests after which the run is aborted as a network error
#define MAX_SILENCE_SECONDS 30    // how long to pad with digital silence waiting for the server to close the last activation

typedef enum {
    RunFinished = 0,     // the session reached a natural end (see on_translation for whether a cry was translated)
    RunInitFailed = 1,   // the HTTP layer could not be initialized
    RunFileError = 2,    // the audio file could not be opened
    RunAuthFailed = 3,   // the server rejected the credentials (HTTP 401/403)
    RunCancelled = 4,    // the run was stopped through the should_stop callback
    RunBadFormat = 5,    // the audio file is not an accepted format (WAV, 1 channel, 16KHz)
    RunTooShort = 6,     // the audio file is shorter than MIN_AUDIO_SECONDS
    RunNetworkError = 7  // MAX_REQUEST_FAILURES requests in a row failed (endpoint unreachable or broken)
} RunResult;

typedef struct {
    const char* endpoint_url;
    const char* api_key;
    const char* user_id;
    const char* audio_file_path;
    int max_loops; // see DEFAULT_MAX_LOOPS
} RunOptions;

/* Progress of a run, reported before every buffer that is about to be sent. */
typedef struct {
    double position_seconds; // current read position within the file (resets when the file loops)
    double total_seconds;    // total duration of the file
    int loop_number;         // which pass through the file this is, starting at 1
    int padding_silence;     // non-zero once the file has ended and digital silence is being sent
} RunProgress;

/* All callbacks are optional (may be NULL) and are invoked from the thread that called client_run. */
typedef struct {
    int (*should_stop)(void* ctx);  // polled between audio buffers; return non-zero to cancel the run
    void (*on_translation)(void* ctx, Answer answer, Reason reason); // called for every valid translation received
    void (*on_progress)(void* ctx, const RunProgress* progress);
    void* ctx;
} RunCallbacks;

/* Runs one full client session, blocking until it finishes. Log output goes through zc_log. */
RunResult client_run(const RunOptions* options, const RunCallbacks* callbacks);

#endif
