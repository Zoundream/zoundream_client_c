#include <stdio.h>
#include <stdlib.h>

#include "client_core.h"
#include "api.h"

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: %s ENDPOINT_URL AUDIO_FILE_PATH\n", argv[0]);
        printf("  ENDPOINT_URL: the url of the Zoundream endpoint you would like to call.\n");
        printf("  AUDIO_FILE_PATH: the path of the audio file that you would like to translate.\n");
        exit(2);
    }

    RunOptions options = {
        .endpoint_url = argv[1],
        .api_key = API_KEY,
        .user_id = TEST_USER_ID,
        .audio_file_path = argv[2],
        .max_loops = DEFAULT_MAX_LOOPS,
    };

    switch (client_run(&options, NULL)) {
        case RunFinished: return 0;
        case RunInitFailed: return 1;
        case RunFileError: return 2;
        case RunBadFormat: return 2;
        case RunTooShort: return 2;
        case RunAuthFailed: return EXIT_AUTH_FAILED;
        default: return 1;
    }
}
