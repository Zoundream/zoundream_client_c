#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "api.h"

#define SEND_TO_SERVER_SIZE_MS 1000 // we send 1 second of audio to the server with every API call
#define SAMPLE_RATE 16000           // this is the number of samples in 1 second of audio
#define BLOCKS_IN_ONE_SECOND 10     // we analyze the audio every 100 ms, so we do 10 analysis in each second
#define THRESHOLD_ANALYSIS_SIZE (SAMPLE_RATE / BLOCKS_IN_ONE_SECOND)
#define GATE_THRESHOLD -20.0        // only blocks of audio where at least some segments pass this threshold will open an activation

#define TRUE 1
#define FALSE 0

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: %s ENDPOINT_URL AUDIO_FILE_PATH\n", argv[0]);
        printf("ENDPOINT_URL: the url of the Zoundream endpoint you would like to call.\n");
        printf("AUDIO_FILE_URL: the path of the audio file that you would like to translate.\n");
        exit(2);
    }

    int is_activation_open = FALSE;
    int is_audio_above_threshold = FALSE;
    uint32_t activation_timestamp = 0;
    ApiResponse api_response;
    size_t audio_block = 0;
    uint16_t audio[SAMPLE_RATE];
    memset(audio, 0, sizeof(int16_t) * SAMPLE_RATE);

    // Initialize the HTTP and audio modules, and if any of them fails just quit
    if (api_init(argv[1]) != 1) exit(1);
    SNDFILE* audio_file = audio_open(argv[2]);
    if (audio_file == 0) exit(2);

    // Read audio in blocks of 100ms and save it in the main audio buffer
    uint16_t* block_position = audio;
    while (audio_read(audio_file, block_position, THRESHOLD_ANALYSIS_SIZE) != 0) {
        audio_block++;

        if (is_activation_open == FALSE) {
            // If the activation is not open, analyze every segment of audio that we receive
            // to check if they are above the threshold.
            // If the activation is already open, we don't need to do any analysis because we need to send the audio anyway.
            if (audio_calculate_rms(block_position, THRESHOLD_ANALYSIS_SIZE) > GATE_THRESHOLD) {
                printf("Block %ld is above threshold\n", audio_block - 1);
                is_audio_above_threshold = TRUE;
            }
        }

        // Check if the buffer is full
        if (audio_block >= BLOCKS_IN_ONE_SECOND) {
            // If the buffer is full and the activation is already open, send this buffer to the API
            if (is_activation_open == TRUE) {
                api_send_audio(audio, activation_timestamp, &api_response);
                // If the server says that the phase is "done" or there's an error, then close the activation. Otherwise increment the timestamp.
                if (api_response.phase == PhaseDone || api_response.phase == PhaseError) {
                    activation_timestamp = 0;
                    is_activation_open = FALSE;
                } else {
                    activation_timestamp += SEND_TO_SERVER_SIZE_MS;
                }
            } else {
                // If the activation is not already open, check if any of the audio in the buffer was above the threshold.
                if (is_audio_above_threshold == TRUE) {
                    // If audio was above the threshold, open the activation and send the buffer to the API, otherwise just discard this buffer.
                    is_activation_open = TRUE;
                    activation_timestamp = 0;
                    api_send_audio(audio, activation_timestamp, &api_response);
                    // If the server says that the phase is "done" or there's an error, then close the activation. Otherwise increment the timestamp.
                    if (api_response.phase == PhaseDone || api_response.phase == PhaseError) {
                        activation_timestamp = 0;
                        is_activation_open = FALSE;
                    } else {
                        activation_timestamp += SEND_TO_SERVER_SIZE_MS;
                    }
                }
            }

            // Reset the buffer and continue analyzing more audio
            is_audio_above_threshold = FALSE;
            audio_block = 0;
        }

        block_position = audio + (audio_block * THRESHOLD_ANALYSIS_SIZE);
    }
}