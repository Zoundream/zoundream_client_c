#include <string.h>

#include "client_core.h"
#include "audio.h"
#include "api.h"
#include "zc_log.h"

#define TRUE 1
#define FALSE 0

static int should_stop(const RunCallbacks* callbacks)
{
    return callbacks && callbacks->should_stop && callbacks->should_stop(callbacks->ctx);
}

RunResult client_run(const RunOptions* options, const RunCallbacks* callbacks)
{
    RunResult result = RunFinished;

    uint32_t activation_timestamp = 0;
    ApiResponse api_response;
    int16_t audio[SAMPLE_RATE];
    memset(audio, 0, sizeof(int16_t) * SAMPLE_RATE);
    int reached_eof = FALSE;
    int loop_count = 0;
    int failed_requests = 0; // consecutive failed requests (reset by every request that gets through)

    // Open and validate the audio file before anything is sent to the API
    zc_log("Processing file: %s", options->audio_file_path);
    int bad_format = 0;
    AudioFile* audio_file = audio_open(options->audio_file_path, &bad_format);
    if (audio_file == 0) {
        return bad_format ? RunBadFormat : RunFileError;
    }
    if (audio_total_frames(audio_file) < (uint64_t) MIN_AUDIO_SECONDS * SAMPLE_RATE) {
        zc_log("File is too short: minimum duration is %d seconds. Skipping it.", MIN_AUDIO_SECONDS);
        audio_close(audio_file);
        return RunTooShort;
    }

    // Initialize the HTTP module, and if it fails just quit
    if (api_init(options->endpoint_url, options->api_key, options->user_id) != 1) {
        audio_close(audio_file);
        return RunInitFailed;
    }

    // Read the audio one second at a time and send every buffer to the API, until the first
    // valid translation arrives. Unlike a real device, we do not apply any volume threshold:
    // all the audio is sent. At the end of the file we loop back to the start (the stream stays
    // continuous, as if the baby kept crying), bounded by max_loops.
    while (audio_read(audio_file, audio, SAMPLE_RATE, TRUE, &reached_eof) != 0) {
        if (should_stop(callbacks)) {
            result = RunCancelled;
            break;
        }

        if (callbacks && callbacks->on_progress) {
            RunProgress progress;
            progress.position_seconds = (double) audio_position_frames(audio_file) / SAMPLE_RATE;
            progress.total_seconds = (double) audio_total_frames(audio_file) / SAMPLE_RATE;
            progress.loop_number = loop_count + 1;
            callbacks->on_progress(callbacks->ctx, &progress);
        }

        api_send_audio(audio, activation_timestamp, &api_response);

        if (api_response.reason == ReasonAuthFailed) {
            result = RunAuthFailed;
            break;
        }
        if (should_stop(callbacks)) {
            result = RunCancelled;
            break;
        }

        // A single failed request is tolerated (the activation is closed and the audio that
        // follows opens a new one), but if nothing gets through the endpoint is unreachable
        // or broken and there is no point playing the rest of the file against it.
        if (api_response.request_failed) {
            failed_requests++;
            if (failed_requests >= MAX_REQUEST_FAILURES) {
                zc_log("%d requests in a row failed. Giving up on this file.", failed_requests);
                result = RunNetworkError;
                break;
            }
        } else {
            failed_requests = 0;
        }

        if (api_response.phase == PhaseDone || api_response.phase == PhaseError) {
            // The server has closed the activation. A valid translation is any answer other
            // than "no_cry" (AnswerUnknown means we failed to parse the response, so it does
            // not count as a valid translation either) - report it and end the run right away.
            if (api_response.answer != AnswerNoCry && api_response.answer != AnswerUnknown) {
                zc_log("Received a valid cry translation. Exiting.");
                if (callbacks && callbacks->on_translation) {
                    callbacks->on_translation(callbacks->ctx, api_response.answer, api_response.reason);
                }
                break;
            }
            // No translation: the next buffer we send (with timestamp 0) starts a brand new activation.
            activation_timestamp = 0;
        } else {
            // The activation is still open, keep sending audio with an increasing timestamp.
            activation_timestamp += SEND_TO_SERVER_SIZE_MS;
        }

        // At the end of the file, loop back to the start so the server keeps receiving continuous
        // real audio, which gives it the best chance to find and translate a cry. This is bounded
        // by max_loops so a file with no valid cry content does not loop forever.
        if (reached_eof) {
            loop_count++;
            if (loop_count > options->max_loops) {
                zc_log("Reached the end of the file with no valid translation after looping back %d times. Exiting.", options->max_loops);
                break;
            }
            zc_log("Reached the end of the file with no valid translation yet. Looping back to keep searching (%d/%d).", loop_count, options->max_loops);
        }
    }

    audio_close(audio_file);
    api_finish();
    return result;
}
