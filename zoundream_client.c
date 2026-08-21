#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "audio.h"
#include "api.h"

#define SEND_TO_SERVER_SIZE_MS 1000 // we send 1 second of audio to the server with every API call
#define SAMPLE_RATE 16000           // this is the number of samples in 1 second of audio
#define BLOCKS_IN_ONE_SECOND 10     // we analyze the audio every 100 ms, so we do 10 analysis in each second
#define THRESHOLD_ANALYSIS_SIZE (SAMPLE_RATE / BLOCKS_IN_ONE_SECOND)
#define GATE_THRESHOLD -20.0        // only blocks of audio where at least some segments pass this threshold will open an activation
#define MAX_LOOPS 3                 // max number of times to loop back to the start looking for a valid translation, to avoid looping forever

#define TRUE 1
#define FALSE 0

int main(int argc, char **argv)
{
    double gate_threshold = GATE_THRESHOLD;
    int gate_disabled = FALSE;

    int opt;
    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
            case 't':
                if (strcmp(optarg, "off") == 0) {
                    gate_disabled = TRUE;
                } else {
                    char* end;
                    gate_threshold = strtod(optarg, &end);
                    if (*end != '\0') {
                        fprintf(stderr, "Invalid threshold value: %s\n", optarg);
                        exit(2);
                    }
                }
                break;
            default:
                fprintf(stderr, "Usage: %s [-t THRESHOLD|off] ENDPOINT_URL AUDIO_FILE_PATH\n", argv[0]);
                exit(2);
        }
    }

    if (argc - optind < 2) {
        printf("Usage: %s [-t THRESHOLD|off] ENDPOINT_URL AUDIO_FILE_PATH\n", argv[0]);
        printf("  ENDPOINT_URL: the url of the Zoundream endpoint you would like to call.\n");
        printf("  AUDIO_FILE_PATH: the path of the audio file that you would like to translate.\n");
        printf("  -t THRESHOLD: gate threshold in dB (default: %.1f). Use 'off' to always send audio.\n", GATE_THRESHOLD);
        exit(2);
    }

    const char* endpoint_url = argv[optind];
    const char* audio_file_path = argv[optind + 1];

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
    if (api_init(endpoint_url) != 1) exit(1);
    printf("Processing file: %s\n", audio_file_path);
    SNDFILE* audio_file = audio_open(audio_file_path);
    if (audio_file == 0) exit(2);

    // Read audio in blocks of 100ms and save it in the main audio buffer.
    // How the end of the file is handled (loop the file with real audio, or pad with digital
    // silence) is decided by loop_on_eof, which is updated at the bottom of the loop.
    int16_t* block_position = audio;
    while (audio_read(audio_file, block_position, THRESHOLD_ANALYSIS_SIZE, loop_on_eof, &reached_eof) != 0) {
        audio_block++;
        if (reached_eof) reached_eof_in_buffer = TRUE;

        if (is_activation_open == FALSE) {
            // If the activation is not open, analyze every segment of audio that we receive
            // to check if they are above the threshold.
            // If the activation is already open, we don't need to do any analysis because we need to send the audio anyway.
            if (gate_disabled || audio_calculate_rms(block_position, THRESHOLD_ANALYSIS_SIZE) > gate_threshold) {
                if (!gate_disabled) printf("Block %ld is above threshold\n", audio_block - 1);
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
                        printf("Received a valid cry translation.\n");
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
            //   This is bounded by MAX_LOOPS so a file with no valid cry content does not loop forever.
            // - YES we have one translation: continue playing the file until the end, without looping.
            //   If a translation is still in progress at the end of the file, pad it with digital silence
            //   (zero values) so the server can finish it.
            if (reached_eof_in_buffer) {
                if (!has_valid_translation) {
                    loop_count++;
                    if (loop_count > MAX_LOOPS) {
                        printf("Reached the end of the file with no valid translation after looping back %d times. Exiting.\n", MAX_LOOPS);
                        break;
                    }
                    printf("Reached the end of the file with no valid translation yet. Looping back to keep searching (%d/%d).\n", loop_count, MAX_LOOPS);
                } else if (is_activation_open == FALSE) {
                    printf("Reached the end of the file with a valid translation. Exiting.\n");
                    break;
                } else {
                    // A translation (beyond the first one) is still in progress: pad every API request with digital silence.
                    // This allows the server to finish it cleanly. Activations always finish given enough data, so there is
                    // no need to cap how much silence we send.
                    if (!silence_announced) {
                        printf("Reached the end of the file with a translation still in progress. Sending digital silence to let it finish.\n");
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
    return 0;
}
