#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "client_core.h"
#include "api.h"

int main(int argc, char **argv)
{
    RunOptions options = {
        .api_key = API_KEY,
        .user_id = TEST_USER_ID,
        .gate_threshold = DEFAULT_GATE_THRESHOLD,
        .gate_disabled = 0,
        .max_loops = DEFAULT_MAX_LOOPS,
    };

    int opt;
    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
            case 't':
                if (strcmp(optarg, "off") == 0) {
                    options.gate_disabled = 1;
                } else {
                    char* end;
                    options.gate_threshold = strtod(optarg, &end);
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
        printf("  -t THRESHOLD: gate threshold in dB (default: %.1f). Use 'off' to always send audio.\n", DEFAULT_GATE_THRESHOLD);
        exit(2);
    }

    options.endpoint_url = argv[optind];
    options.audio_file_path = argv[optind + 1];

    switch (client_run(&options, NULL)) {
        case RunFinished: return 0;
        case RunInitFailed: return 1;
        case RunFileError: return 2;
        case RunAuthFailed: return EXIT_AUTH_FAILED;
        default: return 1;
    }
}
