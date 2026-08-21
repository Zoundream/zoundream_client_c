#ifndef __AUDIO__
#define __AUDIO__

#include <sndfile.h>

#define SAMPLE_RATE 16000

SNDFILE* audio_open(const char* file_path);
int audio_read(SNDFILE* file, int16_t* buffer, size_t amount, int loop_on_eof, int* reached_eof);
#endif
