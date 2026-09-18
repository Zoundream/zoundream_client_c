#ifndef __AUDIO__
#define __AUDIO__

#include <stdint.h>
#include <stddef.h>

#define MAX_SAMPLE_RATE 16000 // the highest accepted sample rate; can be used to size buffers

typedef struct AudioFile AudioFile;

/* Opens an audio file, accepting only WAV files with 1 channel at 8000 or 16000 Hz.
 * The path is UTF-8 encoded on every platform, including Windows (see zc_io.h).
 *
 * On failure returns 0 and, when the optional out parameters are given:
 * - *bad_format is set to 1 when the file was readable but is not an accepted format
 *   (and 0 when the file could not be opened at all);
 * - error is filled with a short human-readable reason (e.g. "2 audio channels
 *   (only mono is accepted)").
 */
AudioFile* audio_open(const char* file_path, int* bad_format, char* error, size_t error_size);
int audio_read(AudioFile* file, int16_t* buffer, size_t amount, int loop_on_eof, int* reached_eof);
void audio_close(AudioFile* file);

int audio_sample_rate(AudioFile* file);

/* Position information, mainly for progress reporting (one frame == one sample, files are mono). */
uint64_t audio_total_frames(AudioFile* file);
uint64_t audio_position_frames(AudioFile* file);

#endif
