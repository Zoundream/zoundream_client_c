#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "audio.h"
#include "api.h"

#define MAX_TIMESTAMP_LEN 30 // size of "x-audio-timestamp: " plus the maximum size of an uint32 converted to string, plus null termination.

struct ResponseBody {
  char *memory;
  size_t size;
};
struct ResponseBody response;
CURL *curl;

/* ------------------------------ Internal functions ----------------------------------- */

static size_t read_response_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct ResponseBody *mem = (struct ResponseBody *)userp;

    // reallocate the buffer, making it bigger as needed...
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "Not enough memory (realloc returned NULL)\n");
        exit(1);
    }
    mem->memory = ptr;

    // ...then copy the data form the response into it, and update the size counter
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

Phase parse_phase(const char* phase) {
    if (phase == NULL) return PhaseError;
    else if (strcmp(phase, "detecting") == 0) return PhaseDetecting;
    else if (strcmp(phase, "translating") == 0) return PhaseTranslating;
    else if (strcmp(phase, "done") == 0) return PhaseDone;
    else return PhaseError;
}

Answer parse_answer(const char* answer) {
    if (answer == NULL) return AnswerUnknown;
    else if (strcmp(answer, "no_cry") == 0) return AnswerNoCry;
    else if (strcmp(answer, "burp") == 0) return AnswerBurp;
    else if (strcmp(answer, "sleep") == 0) return AnswerSleep;
    else if (strcmp(answer, "hungry") == 0) return AnswerHungry;
    else if (strcmp(answer, "pain") == 0) return AnswerPain;
    else if (strcmp(answer, "uncomfortable") == 0) return AnswerUncomfortable;
    else return AnswerUnknown;
}

int parse_response(ApiResponse* api_response) {
    struct json_object *parsed_json;
    struct json_object *phase;
    struct json_object *answer;

    parsed_json = json_tokener_parse(response.memory);

    if (json_object_object_get_ex(parsed_json, "phase", &phase)) {
        const char* phase_value = json_object_get_string(phase);
        api_response->phase = parse_phase(phase_value);
    }

    if (json_object_object_get_ex(parsed_json, "answer", &answer)) {
        const char *answer_value = json_object_get_string(answer);
        api_response->answer = parse_answer(answer_value);
    }
}


/* ------------------------------ Exported functions ----------------------------------- */

/** Initializes the HTTP library.
 *
 * @params endpoint_url: the URL of the endpoint to use for all API calls
 * @returns 1 if successful, otherwise 0
 */
int api_init(const char* endpoint_url)
{
    response.memory = malloc(1);
    response.size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, read_response_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
        curl_easy_setopt(curl, CURLOPT_URL, endpoint_url);
        curl_easy_setopt(curl, CURLOPT_POST, 1);
        return 1;
    } else {
        return 0;
    }
}

/** Cleans up the HTTP library.
 *
 */
void api_finish() {
    free(response.memory);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
}

/** Sends a block of audio to the server, with the specified timestamp, and waits for a response
 *
 * In case the request to the server fails due to network errors, we simply return a PhaseError in api_response.phase
 * In reality, depending on the error, the client could retry sendind the data, as explained in the documentation.
 *
 * In case of success, api_response.phase will contain the current phase of the activation.
 * If the activation has been closed by the server, api_response.phase will be PhaseDone, and the cry translation answer
 * will be available in api_response.answer
 *
 * @param audio a buffer containing the audio to send
 * @param timestamp the timestamp (relative to the start of the activation) of the audio
 * @param api_response a pointer to the structure where the response will be stored.
 */
void api_send_audio(int16_t* audio, u_int32_t timestamp, ApiResponse* api_response) {
    response.size = 0; // restart reading the response, overwriting the existing buffer

    printf("Sending audio for timestamp %d : ", timestamp);
    CURLcode res;

    char timestamp_header [MAX_TIMESTAMP_LEN];
    int result = snprintf(timestamp_header, MAX_TIMESTAMP_LEN - 1, "x-audio-timestamp: %u", timestamp);
    if (result < 0 && result >= MAX_TIMESTAMP_LEN) {
        fprintf(stderr, "Failed to convert timestamp to string: %u => %d\n", timestamp, result);
        api_response->phase = PhaseError;
    }

    struct curl_slist *list = NULL;
    list = curl_slist_append(list, "Authorization: curl");
    list = curl_slist_append(list, "x-audio-sample-rate: 16000");
    list = curl_slist_append(list, timestamp_header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, audio);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, SAMPLE_RATE * sizeof(int16_t));

    res = curl_easy_perform(curl);
    curl_slist_free_all(list);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        api_response->phase = PhaseError;
    }

    // The response is a block of JSON data, which we need to parse
    parse_response(api_response);

    printf("Response (%lu bytes): %s\n", (unsigned long) response.size, response.memory);
}
