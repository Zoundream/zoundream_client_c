#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "api.h"

#define SEND_TO_SERVER_SIZE_MS 1000 // we send 1 second of audio to the server with every API call
#define MAX_LOOPS 3                 // max number of times to loop back to the start looking for a valid translation, to avoid looping forever

#define TRUE 1
#define FALSE 0

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: %s ENDPOINT_URL AUDIO_FILE_PATH\n", argv[0]);
        printf("  ENDPOINT_URL: the url of the Zoundream endpoint you would like to call.\n");
        printf("  AUDIO_FILE_PATH: the path of the audio file that you would like to translate.\n");
        exit(2);
    }

    const char* endpoint_url = argv[1];
    const char* audio_file_path = argv[2];

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
    if (api_init(endpoint_url) != 1) exit(1);
    printf("Processing file: %s\n", audio_file_path);
    SNDFILE* audio_file = audio_open(audio_file_path);
    if (audio_file == 0) exit(2);

    // Read the audio one second at a time and send every buffer to the API.
    // Unlike a real device, we do not apply any volume threshold: all the audio is sent.
    // How the end of the file is handled (loop the file with real audio, or pad with digital
    // silence) is decided by loop_on_eof, which is updated at the bottom of the loop.
    while (audio_read(audio_file, audio, SAMPLE_RATE, loop_on_eof, &reached_eof) != 0) {
        api_send_audio(audio, activation_timestamp, &api_response);

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
                printf("Received a valid cry translation.\n");
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
        //   This is bounded by MAX_LOOPS so a file with no valid cry content does not loop forever.
        // - YES we have one translation: continue playing the file until the end, without looping.
        //   If a translation is still in progress at the end of the file, pad it with digital silence
        //   (zero values) so the server can finish it.
        if (reached_eof) {
            if (!has_valid_translation) {
                loop_count++;
                if (loop_count > MAX_LOOPS) {
                    printf("Reached the end of the file with no valid translation after looping back %d times. Exiting.\n", MAX_LOOPS);
                    break;
                }
                printf("Reached the end of the file with no valid translation yet. Looping back to keep searching (%d/%d).\n", loop_count, MAX_LOOPS);
            } else if (closed) {
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

        // While searching for the first valid translation we loop the file (continuous real audio).
        // When we have at least one, we stop looping and pad with digital silence instead.
        loop_on_eof = !has_valid_translation;
    }

    sf_close(audio_file);
    api_finish();
    return 0;
}
