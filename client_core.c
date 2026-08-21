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
    int has_valid_translation = FALSE;
    int reached_eof = FALSE;
    int loop_count = 0;
    int silence_announced = FALSE; // whether we have already logged that we are padding with silence
    int loop_on_eof = TRUE;        // whether audio_read should loop the file (vs pad silence) at EOF

    // Initialize the HTTP and audio modules, and if any of them fails just quit
    if (api_init(options->endpoint_url, options->api_key, options->user_id) != 1) return RunInitFailed;
    zc_log("Processing file: %s", options->audio_file_path);
    AudioFile* audio_file = audio_open(options->audio_file_path);
    if (audio_file == 0) {
        api_finish();
        return RunFileError;
    }

    // Read the audio one second at a time and send every buffer to the API.
    // Unlike a real device, we do not apply any volume threshold: all the audio is sent.
    // How the end of the file is handled (loop the file with real audio, or pad with digital
    // silence) is decided by loop_on_eof, which is updated at the bottom of the loop.
    while (audio_read(audio_file, audio, SAMPLE_RATE, loop_on_eof, &reached_eof) != 0) {
        if (should_stop(callbacks)) {
            result = RunCancelled;
            break;
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

        int closed = (api_response.phase == PhaseDone || api_response.phase == PhaseError);

        if (closed) {
            // The server has closed the activation. The next buffer we send (with timestamp 0)
            // will start a brand new activation, so reset the timestamp and the silence counter.
            activation_timestamp = 0;
            silence_announced = FALSE;

            // A valid translation is any answer other than "no_cry" (AnswerUnknown means we
            // failed to parse the response, so it does not count as a valid translation either).
            if (api_response.answer != AnswerNoCry && api_response.answer != AnswerUnknown) {
                has_valid_translation = TRUE;
                zc_log("Received a valid cry translation.");
                if (callbacks && callbacks->on_translation) {
                    callbacks->on_translation(callbacks->ctx, api_response.answer, api_response.reason);
                }
            }
        } else {
            // The activation is still open, keep sending audio with an increasing timestamp.
            activation_timestamp += SEND_TO_SERVER_SIZE_MS;
        }

        // What we do at the end of the file depends only on whether we already have at least one valid
        // translation or not:
        // - NO translation yet: loop the file so the server keeps receiving
        //   continuous real audio, which gives it the best chance to find and translate a cry. We
        //   never pad with silence in this case, as we want to simulate a baby continuing to cry.
        //   This is bounded by max_loops so a file with no valid cry content does not loop forever.
        // - YES we have one translation: continue playing the file until the end, without looping.
        //   If a translation is still in progress at the end of the file, pad it with digital silence
        //   (zero values) so the server can finish it.
        if (reached_eof) {
            if (!has_valid_translation) {
                loop_count++;
                if (loop_count > options->max_loops) {
                    zc_log("Reached the end of the file with no valid translation after looping back %d times. Exiting.", options->max_loops);
                    break;
                }
                zc_log("Reached the end of the file with no valid translation yet. Looping back to keep searching (%d/%d).", loop_count, options->max_loops);
            } else if (closed) {
                zc_log("Reached the end of the file with a valid translation. Exiting.");
                break;
            } else {
                // A translation (beyond the first one) is still in progress: pad every API request with digital silence.
                // This allows the server to finish it cleanly. Activations always finish given enough data, so there is
                // no need to cap how much silence we send.
                if (!silence_announced) {
                    zc_log("Reached the end of the file with a translation still in progress. Sending digital silence to let it finish.");
                    silence_announced = TRUE;
                }
            }
        }

        // While searching for the first valid translation we loop the file (continuous real audio).
        // When we have at least one, we stop looping and pad with digital silence instead.
        loop_on_eof = !has_valid_translation;
    }

    audio_close(audio_file);
    api_finish();
    return result;
}
