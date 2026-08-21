#include <string.h>

#include "client_core.h"
#include "audio.h"
#include "api.h"
#include "zc_log.h"

#define BLOCKS_IN_ONE_SECOND 10 // we analyze the audio every 100 ms, so we do 10 analysis in each second
#define THRESHOLD_ANALYSIS_SIZE (SAMPLE_RATE / BLOCKS_IN_ONE_SECOND)

#define TRUE 1
#define FALSE 0

static int should_stop(const RunCallbacks* callbacks)
{
    return callbacks && callbacks->should_stop && callbacks->should_stop(callbacks->ctx);
}

RunResult client_run(const RunOptions* options, const RunCallbacks* callbacks)
{
    RunResult result = RunFinished;

    int is_activation_open = FALSE;
    int is_audio_above_threshold = FALSE;
    uint32_t activation_timestamp = 0;
    ApiResponse api_response;
    size_t audio_block = 0;
    int16_t audio[SAMPLE_RATE];
    memset(audio, 0, sizeof(int16_t) * SAMPLE_RATE);
    int has_valid_translation = FALSE;
    int reached_eof = FALSE;           // set by audio_read when a single 100ms read hits the end of the file
    int reached_eof_in_buffer = FALSE; // whether any of the reads of the current 1 second buffer hit the end of the file
    int loop_count = 0;
    int silence_announced = FALSE;     // whether we have already logged that we are padding with silence
    int loop_on_eof = TRUE;            // whether audio_read should loop the file (vs pad silence) at EOF

    // Initialize the HTTP and audio modules, and if any of them fails just quit
    if (api_init(options->endpoint_url, options->api_key, options->user_id) != 1) return RunInitFailed;
    zc_log("Processing file: %s", options->audio_file_path);
    SNDFILE* audio_file = audio_open(options->audio_file_path);
    if (audio_file == 0) {
        api_finish();
        return RunFileError;
    }

    // Read audio in blocks of 100ms and save it in the main audio buffer.
    // How the end of the file is handled (loop the file with real audio, or pad with digital
    // silence) is decided by loop_on_eof, which is updated at the bottom of the loop.
    int16_t* block_position = audio;
    while (audio_read(audio_file, block_position, THRESHOLD_ANALYSIS_SIZE, loop_on_eof, &reached_eof) != 0) {
        if (should_stop(callbacks)) {
            result = RunCancelled;
            break;
        }

        audio_block++;
        if (reached_eof) reached_eof_in_buffer = TRUE;

        if (is_activation_open == FALSE) {
            // If the activation is not open, analyze every segment of audio that we receive
            // to check if they are above the threshold.
            // If the activation is already open, we don't need to do any analysis because we need to send the audio anyway.
            if (options->gate_disabled || audio_calculate_rms(block_position, THRESHOLD_ANALYSIS_SIZE) > options->gate_threshold) {
                if (!options->gate_disabled) zc_log("Block %ld is above threshold", audio_block - 1);
                is_audio_above_threshold = TRUE;
            }
        }

        // Check if the buffer is full
        if (audio_block >= BLOCKS_IN_ONE_SECOND) {
            // Send the buffer if the activation is already open, or open a new activation if
            // any of the audio in the buffer was above the threshold. Otherwise discard the buffer.
            int sent = FALSE;
            if (is_activation_open == TRUE) {
                api_send_audio(audio, activation_timestamp, &api_response);
                sent = TRUE;
            } else if (is_audio_above_threshold == TRUE) {
                is_activation_open = TRUE;
                activation_timestamp = 0;
                api_send_audio(audio, activation_timestamp, &api_response);
                sent = TRUE;
            }

            if (sent) {
                if (api_response.reason == ReasonAuthFailed) {
                    result = RunAuthFailed;
                    break;
                }
                if (should_stop(callbacks)) {
                    result = RunCancelled;
                    break;
                }

                if (api_response.phase == PhaseDone || api_response.phase == PhaseError) {
                    // The server has closed the activation. The next buffer we send (with timestamp 0)
                    // will start a brand new activation, so reset the timestamp and the silence counter.
                    activation_timestamp = 0;
                    is_activation_open = FALSE;
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
            }

            // What we do at the end of the file depends only on whether we already have at least one valid
            // translation or not:
            // - NO translation yet: loop the file so the server keeps receiving continuous real audio,
            //   which gives it the best chance to find and translate a cry. We never pad with silence in
            //   this case, as we want to simulate a baby continuing to cry.
            //   This is bounded by max_loops so a file with no valid cry content does not loop forever.
            // - YES we have one translation: continue playing the file until the end, without looping.
            //   If a translation is still in progress at the end of the file, pad it with digital silence
            //   (zero values) so the server can finish it.
            if (reached_eof_in_buffer) {
                if (!has_valid_translation) {
                    loop_count++;
                    if (loop_count > options->max_loops) {
                        zc_log("Reached the end of the file with no valid translation after looping back %d times. Exiting.", options->max_loops);
                        break;
                    }
                    zc_log("Reached the end of the file with no valid translation yet. Looping back to keep searching (%d/%d).", loop_count, options->max_loops);
                } else if (is_activation_open == FALSE) {
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

            // Reset the buffer and continue analyzing more audio
            is_audio_above_threshold = FALSE;
            audio_block = 0;
            reached_eof_in_buffer = FALSE;

            // While searching for the first valid translation we loop the file (continuous real audio).
            // When we have at least one, we stop looping and pad with digital silence instead.
            loop_on_eof = !has_valid_translation;
        }

        block_position = audio + (audio_block * THRESHOLD_ANALYSIS_SIZE);
    }

    sf_close(audio_file);
    api_finish();
    return result;
}
