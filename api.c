#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include "third_party/cJSON.h"
#include "audio.h"
#include "api.h"
#include "zc_log.h"

struct ResponseBody {
  char *memory;
  size_t size;
};
struct ResponseBody response;
CURL *curl;

static char auth_header[128];
static char api_key_header[160];

static int (*abort_check)(void* ctx) = NULL;
static void* abort_check_ctx = NULL;

// Timestamp of the previous send, used by the pacing code. Reset by api_init.
static struct timespec previous_send = { 0, 0 };

#define MAX_TIMESTAMP_LEN 30 // size of the string "x-audio-timestamp: " plus the maximum size of an uint32 converted to string, plus null termination.

/* ------------------------------ Internal functions ----------------------------------- */

static int aborted()
{
    return abort_check != NULL && abort_check(abort_check_ctx) != 0;
}

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

// Polled by curl during the transfer; returning non-zero aborts the transfer.
static int transfer_progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)clientp; (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    return aborted() ? 1 : 0;
}

// This demo program reads audio from the file system, but a real device will instead use a microphone.
// By definition to read 1 second of audio from a microphone will take 1 second of real time.
// This function helps simulate this behavior by waiting for the right amount of real time to pass before
// sending the next buffer to the server.
static void wait_for_send_slot()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (previous_send.tv_sec != 0 || previous_send.tv_nsec != 0) {
        long elapsed_ms = (now.tv_sec - previous_send.tv_sec) * 1000 + (now.tv_nsec - previous_send.tv_nsec) / 1000000;
        long remaining_ms = SEND_TO_SERVER_SIZE_MS - elapsed_ms;
        // Wait in small slices so an abort request (e.g. the GUI Stop button) is honored quickly.
        while (remaining_ms > 0 && !aborted()) {
            long slice_ms = remaining_ms < 50 ? remaining_ms : 50;
            struct timespec pause = { slice_ms / 1000, (slice_ms % 1000) * 1000000L };
            nanosleep(&pause, NULL);
            remaining_ms -= slice_ms;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &previous_send);
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

Reason parse_reason(const char* reason) {
    if (reason == NULL) return ReasonUnknown;
    else if (strcmp(reason, "no_valid_cry_patterns") == 0) return ReasonNoCryDetected;
    else if (strcmp(reason, "detection_timeout") == 0) return ReasonDetectionTimeout;
    else if (strcmp(reason, "activation_timeout") == 0) return ReasonActivationTimeout;
    else if (strcmp(reason, "no_cry_patterns_timeout") == 0) return ReasonNoCryPatternsTimeout;
    else if (strcmp(reason, "activation_already_closed") == 0) return ReasonActivationAlreadyClosed;
    else if (strcmp(reason, "timestamp_out_of_sequence") == 0) return ReasonTimestampOutOfSequence;
    else if (strcmp(reason, "activation_expired") == 0) return ReasonActivationExpired;
    else if (strcmp(reason, "cry_translated") == 0) return ReasonCryTranslated;
    else return ReasonUnknown;
}

void parse_response(ApiResponse* api_response) {
    cJSON* parsed_json = cJSON_Parse(response.memory);

    const cJSON* phase = cJSON_GetObjectItemCaseSensitive(parsed_json, "phase");
    if (cJSON_IsString(phase)) {
        api_response->phase = parse_phase(phase->valuestring);
    }

    const cJSON* answer = cJSON_GetObjectItemCaseSensitive(parsed_json, "answer");
    if (cJSON_IsString(answer)) {
        api_response->answer = parse_answer(answer->valuestring);
    }

    const cJSON* reason = cJSON_GetObjectItemCaseSensitive(parsed_json, "reason");
    if (cJSON_IsString(reason)) {
        api_response->reason = parse_reason(reason->valuestring);
    } else {
        api_response->reason = ReasonUnknown;
    }

    cJSON_Delete(parsed_json);
}


/* ------------------------------ Exported functions ----------------------------------- */

/** Initializes the HTTP library.
 *
 * @param endpoint_url the URL of the endpoint to use for all API calls
 * @param api_key the API key assigned to your company
 * @param user_id the base user id; a per-run timestamp is appended to it (see below)
 * @returns 1 if successful, otherwise 0
 */
int api_init(const char* endpoint_url, const char* api_key, const char* user_id)
{
    response.memory = malloc(1);
    response.size = 0;
    previous_send.tv_sec = 0;
    previous_send.tv_nsec = 0;

    // Make the user id unique for each run by appending a millisecond timestamp, so that user
    // collisions are impossible while all traffic from one tool remains filterable by its base id.
    // IMPORTANT: this is only for testing and in production the user ID must be unique and stable.
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    long long run_id = (long long) now.tv_sec * 1000 + now.tv_nsec / 1000000;
    snprintf(auth_header, sizeof(auth_header), "Authorization: %s-%lld", user_id, run_id);
    zc_log("Using user id: %s-%lld", user_id, run_id);
    snprintf(api_key_header, sizeof(api_key_header), "x-api-key: %s", api_key);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, read_response_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
        curl_easy_setopt(curl, CURLOPT_URL, endpoint_url);
        curl_easy_setopt(curl, CURLOPT_POST, 1);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress_callback);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
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
    response.memory = NULL;
    curl_easy_cleanup(curl);
    curl_global_cleanup();
}

void api_set_abort_check(int (*check)(void* ctx), void* ctx) {
    abort_check = check;
    abort_check_ctx = ctx;
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
void api_send_audio(int16_t* audio, uint32_t timestamp, ApiResponse* api_response) {
    // Start from a clean, defined state so that a failed request can never leave stale values
    // (e.g. the answer of the previous response) in api_response.
    api_response->phase = PhaseError;
    api_response->answer = AnswerUnknown;
    api_response->reason = ReasonUnknown;

    wait_for_send_slot();

    response.size = 0; // restart reading the response, overwriting the existing buffer

    zc_log("Sending audio for timestamp %u", timestamp);
    CURLcode res;

    char timestamp_header [MAX_TIMESTAMP_LEN];
    int result = snprintf(timestamp_header, MAX_TIMESTAMP_LEN - 1, "x-audio-timestamp: %u", timestamp);
    if (result < 0 || result >= MAX_TIMESTAMP_LEN) {
        zc_log("Failed to convert timestamp to string: %u => %d", timestamp, result);
        return;
    }

    struct curl_slist *list = NULL;
    list = curl_slist_append(list, auth_header);
    list = curl_slist_append(list, api_key_header);
    list = curl_slist_append(list, "x-audio-sample-rate: 16000");
    list = curl_slist_append(list, timestamp_header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, audio);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, SAMPLE_RATE * sizeof(int16_t));

    res = curl_easy_perform(curl);
    curl_slist_free_all(list);

    if (res != CURLE_OK) {
        zc_log("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        return;
    }

    // A 401/403 means the request was rejected before ever reaching the queue (almost
    // always a wrong or missing API key) and will keep being rejected for every subsequent
    // request too, so there is no point continuing: report it so the caller can bail out
    // immediately and loudly instead of silently looping (the response body has no "phase"
    // field for parse_response to recognize, so without this check the run would just carry
    // on regardless).
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code == 401 || http_code == 403) {
        zc_log("Authentication failed (HTTP %ld): %s", http_code, response.memory);
        zc_log("Check your API key.");
        api_response->reason = ReasonAuthFailed;
        return;
    }

    // The response is a block of JSON data, which we need to parse
    parse_response(api_response);

    zc_log("Response (%lu bytes): %s", (unsigned long) response.size, response.memory);
}

const char* api_answer_name(Answer answer) {
    switch (answer) {
        case AnswerNoCry: return "no cry";
        case AnswerBurp: return "burp";
        case AnswerSleep: return "sleep";
        case AnswerHungry: return "hungry";
        case AnswerUncomfortable: return "uncomfortable";
        case AnswerPain: return "pain";
        default: return "unknown";
    }
}

const char* api_reason_name(Reason reason) {
    switch (reason) {
        case ReasonNoCryDetected: return "no valid cry patterns";
        case ReasonDetectionTimeout: return "detection timeout";
        case ReasonActivationTimeout: return "activation timeout";
        case ReasonNoCryPatternsTimeout: return "no cry patterns timeout";
        case ReasonActivationAlreadyClosed: return "activation already closed";
        case ReasonTimestampOutOfSequence: return "timestamp out of sequence";
        case ReasonActivationExpired: return "activation expired";
        case ReasonCryTranslated: return "cry translated";
        case ReasonAuthFailed: return "authentication failed";
        default: return "unknown";
    }
}
