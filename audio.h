#ifndef __AUDIO__
#define __AUDIO__

#include <stdint.h>
#include <stddef.h>

#define SAMPLE_RATE 16000

typedef struct AudioFile AudioFile;

/* bad_format (optional, may be NULL) is set to 1 when the file could be opened but is not
 * an accepted format (WAV, 1 channel, 16KHz), and to 0 in every other case. */
AudioFile* audio_open(const char* file_path, int* bad_format);
int audio_read(AudioFile* file, int16_t* buffer, size_t amount, int loop_on_eof, int* reached_eof);
void audio_close(AudioFile* file);

/* Position information, mainly for progress reporting (one frame == one sample, files are mono). */
uint64_t audio_total_frames(AudioFile* file);
uint64_t audio_position_frames(AudioFile* file);

#endif
