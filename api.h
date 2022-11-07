#ifndef __API__
#define __API___

/* The possible states of an activation */
typedef enum {
    PhaseError = 0,         // An error has occurred, the activation should be closed
    PhaseDetecting = 1,     // We are detecting if the audio you sent contains a cry or if it's some other noise
    PhaseTranslating = 2,   // We think the sound is a cry, and we are translating it
    PhaseDone = 3           // We have translated the cry (see the ApiResponse.answer) or some invalid data was sent
} Phase;

typedef enum {
    AnswerNoCry = 0,          // We analyzed the audio but it's not a cry
    AnswerBurp = 1,           // The baby needs to release some gas
    AnswerSleep = 2,          // The baby needs to sleep
    AnswerHungry = 3,         // The baby needs to eat
    AnswerUncomfortable = 4,  // The baby needs to be cuddled and comforted
    AnswerPain = 5,           // The baby is in physical or emotional distress
    AnswerUnknown = 6         // Special case for when the client fails to parse the response from the server
} Answer;

typedef struct {
   Phase phase;    // the current state of the activation
   Answer answer;  // the answer for the translation (if phase is PhaseDone, otherwise needs to be ignored)
} ApiResponse ;

int api_init(const char* endpoint);
void api_send_audio(int16_t* audio, u_int32_t timestamp, ApiResponse* api_response);
void api_finish();

#endif