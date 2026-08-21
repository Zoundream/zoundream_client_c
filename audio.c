#include "audio.h"
#include "zc_log.h"

#include <stdlib.h>
#include <string.h>

#define DR_WAV_IMPLEMENTATION
#define DRWAV_NO_STDIO_W // we only need the narrow (char*) file APIs
#include "third_party/dr_wav.h"

struct AudioFile {
    drwav wav;
};

/** Opens an audio file for reading.
 *
 * @returns 0 if it fails to open the file, or if the format is not 16KHZ Mono, or a handle to the sound file if successful.
 */
AudioFile* audio_open(const char* file_path) {
    AudioFile* file = malloc(sizeof(AudioFile));
    if (file == NULL) return 0;

    if (!drwav_init_file(&file->wav, file_path, NULL)) {
        zc_log("Failed to open file.");
        free(file);
        return 0;
    }

    zc_log("Audio file opened: channels %d, sample rate %d", file->wav.channels, file->wav.sampleRate);
    zc_log("  Total frames: %llu (duration: %.2f seconds)",
           (unsigned long long) file->wav.totalPCMFrameCount,
           (double) file->wav.totalPCMFrameCount / file->wav.sampleRate);
    if (file->wav.channels != 1 || file->wav.sampleRate != 16000) {
        zc_log("Format not compatible. Can only accept single channel 16KHz files.");
        audio_close(file);
        return 0;
    }
    return file;
}

/** Reads a chunk of audio from the sound file.
 *
 * When the end of the file is reached before `amount` samples have been read, *reached_eof is set
 * to 1 and the rest of the buffer is filled depending on `loop_on_eof`:
 *  - loop_on_eof != 0: the file is rewound and the remainder of the buffer is filled from the start
 *    of the file, so the audio stream stays continuous (no silence is inserted).
 *    Subsequent reads keep going from there, effectively looping the file forever.
 *  - loop_on_eof == 0: the remainder of the buffer is filled with zeros (digital silence), so every
 *    read past the end of the file returns pure silence.
 * (If the file is shorter than a single buffer, any part that still cannot be filled is zeroed.)
 *
 * @param file: a sound file handle from audio_open()
 * @param buffer: the audio buffer into which to save the audio
 * @param amount: the amount of samples (int16_t items) to read into the buffer
 * @param loop_on_eof: if non-zero, loop the file on EOF; if zero, pad with digital silence
 * @param reached_eof: set to 1 if the end of the file was reached during this read, otherwise 0
 * @returns 1 if successful, 0 if an error occurred
 */
int audio_read(AudioFile* file, int16_t* buffer, size_t amount, int loop_on_eof, int* reached_eof) {
    *reached_eof = 0;
    // The file is mono, so one PCM frame is exactly one int16_t sample.
    drwav_uint64 read = drwav_read_pcm_frames_s16(&file->wav, amount, buffer);
    if (read < amount) {
        // Reached the end of the file before filling the buffer.
        *reached_eof = 1;
        size_t filled = (size_t) read;
        if (loop_on_eof) {
            // Loop the file: rewind and keep filling from the start so the stream stays continuous.
            drwav_seek_to_pcm_frame(&file->wav, 0);
            drwav_uint64 more = drwav_read_pcm_frames_s16(&file->wav, amount - filled, buffer + filled);
            filled += (size_t) more;
        }
        if (filled < amount) {
            // Not looping (digital silence), or the file is shorter than one buffer: zero the rest.
            memset(buffer + filled, 0, (amount - filled) * sizeof(int16_t));
        }
    }
    return 1;
}

/** Closes a sound file opened with audio_open and frees its resources. */
void audio_close(AudioFile* file) {
    if (file == NULL) return;
    drwav_uninit(&file->wav);
    free(file);
}
