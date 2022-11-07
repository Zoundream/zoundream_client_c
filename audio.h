#ifndef __AUDIO__
#define __AUDIO___

#include <sndfile.h>

#define SAMPLE_RATE 16000

SNDFILE* audio_open(const char* file_path);
int audio_read(SNDFILE* file, int16_t* buffer, size_t amount);
int audio_calculate_rms(int16_t* buffer, size_t size);
#endif
