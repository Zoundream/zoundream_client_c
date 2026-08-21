#ifndef __AUDIO__
#define __AUDIO__

#include <stdint.h>
#include <stddef.h>

#define SAMPLE_RATE 16000

typedef struct AudioFile AudioFile;

AudioFile* audio_open(const char* file_path);
int audio_read(AudioFile* file, int16_t* buffer, size_t amount, int loop_on_eof, int* reached_eof);
void audio_close(AudioFile* file);

#endif
