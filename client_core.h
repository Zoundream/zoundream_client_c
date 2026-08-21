#ifndef __CLIENT_CORE__
#define __CLIENT_CORE__

#include "api.h"

/* The client session logic (read audio, gate it, send it to the API, decide when to loop
 * the file and when to stop), extracted so that both the command line client and the GUI
 * run exactly the same code.
 */

#define DEFAULT_GATE_THRESHOLD -20.0 // only blocks of audio where at least some segments pass this threshold will open an activation
#define DEFAULT_MAX_LOOPS 3          // max number of times to loop back to the start looking for a valid translation, to avoid looping forever

typedef enum {
    RunFinished = 0,   // the session reached a natural end (see on_translation for whether a cry was translated)
    RunInitFailed = 1, // the HTTP layer could not be initialized
    RunFileError = 2,  // the audio file could not be opened or is not 16KHz mono
    RunAuthFailed = 3, // the server rejected the credentials (HTTP 401/403)
    RunCancelled = 4   // the run was stopped through the should_stop callback
} RunResult;

typedef struct {
    const char* endpoint_url;
    const char* api_key;
    const char* user_id;
    const char* audio_file_path;
    double gate_threshold; // in dB, see DEFAULT_GATE_THRESHOLD
    int gate_disabled;     // if non-zero the gate is bypassed and all audio is sent
    int max_loops;         // see DEFAULT_MAX_LOOPS
} RunOptions;

/* All callbacks are optional (may be NULL) and are invoked from the thread that called client_run. */
typedef struct {
    int (*should_stop)(void* ctx);  // polled between audio blocks; return non-zero to cancel the run
    void (*on_translation)(void* ctx, Answer answer, Reason reason); // called for every valid translation received
    void* ctx;
} RunCallbacks;

/* Runs one full client session, blocking until it finishes. Log output goes through zc_log. */
RunResult client_run(const RunOptions* options, const RunCallbacks* callbacks);

#endif
