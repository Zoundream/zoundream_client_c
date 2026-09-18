#include "audio.h"
#include "zc_io.h"
#include "zc_log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define DR_WAV_IMPLEMENTATION
// We open the file ourselves with zc_fopen() (see zc_io.h: dr_wav's own fopen() would break
// on non-ASCII paths on Windows) and feed dr_wav through the callbacks below, so none of its
// file APIs are needed.
#define DR_WAV_NO_STDIO
#include "third_party/dr_wav.h"

struct AudioFile {
    drwav wav;
    FILE* stream; // the open file dr_wav reads through, closed by audio_close()
};

static void set_error(char* error, size_t error_size, const char* message) {
    if (error && error_size > 0) snprintf(error, error_size, "%s", message);
}

/* ---- The stdio backing for dr_wav, which reads through these three callbacks ---- */

static size_t on_read(void* stream, void* buffer, size_t bytes_to_read) {
    return fread(buffer, 1, bytes_to_read, (FILE*) stream);
}

static drwav_bool32 on_seek(void* stream, int offset, drwav_seek_origin origin) {
    int whence = SEEK_SET;
    if (origin == DRWAV_SEEK_CUR) whence = SEEK_CUR;
    else if (origin == DRWAV_SEEK_END) whence = SEEK_END;
    return fseek((FILE*) stream, offset, whence) == 0;
}

static drwav_bool32 on_tell(void* stream, drwav_int64* cursor) {
    long position = ftell((FILE*) stream);
    if (position < 0) return DRWAV_FALSE;
    *cursor = position;
    return DRWAV_TRUE;
}

/** Opens an audio file for reading. See audio.h for the accepted formats and the out parameters.
 *
 * @returns a handle to the sound file, or 0 on failure.
 */
AudioFile* audio_open(const char* file_path, int* bad_format, char* error, size_t error_size) {
    if (bad_format) *bad_format = 0;
    set_error(error, error_size, "");

    AudioFile* file = malloc(sizeof(AudioFile));
    if (file == NULL) return 0;

    file->stream = zc_fopen(file_path, "rb");
    if (file->stream == NULL) {
        char reason[160];
        snprintf(reason, sizeof(reason), "the file could not be opened (%s)", strerror(errno));
        set_error(error, error_size, reason);
        zc_log("Cannot use this file: %s.", reason);
        free(file);
        return 0;
    }

    if (!drwav_init(&file->wav, on_read, on_seek, on_tell, file->stream, NULL)) {
        set_error(error, error_size, "not a valid WAV file (its header could not be read)");
        zc_log("Cannot use this file: not a valid WAV file (its header could not be read).");
        fclose(file->stream);
        free(file);
        return 0;
    }

    zc_log("Audio file opened: channels %d, sample rate %d", file->wav.channels, file->wav.sampleRate);
    zc_log("  Total frames: %llu (duration: %.2f seconds)",
           (unsigned long long) file->wav.totalPCMFrameCount,
           (double) file->wav.totalPCMFrameCount / file->wav.sampleRate);

    char reason[128] = "";
    if (file->wav.channels != 1) {
        snprintf(reason, sizeof(reason), "%d audio channels (only mono is accepted)", file->wav.channels);
    } else if (file->wav.sampleRate != 8000 && file->wav.sampleRate != 16000) {
        snprintf(reason, sizeof(reason), "%u Hz sample rate (only 8000 or 16000 Hz are accepted)", file->wav.sampleRate);
    }
    if (reason[0] != 0) {
        set_error(error, error_size, reason);
        zc_log("Cannot use this file: %s.", reason);
        if (bad_format) *bad_format = 1;
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
    fclose(file->stream);
    free(file);
}

/** @returns the sample rate of the file, in Hz. */
int audio_sample_rate(AudioFile* file) {
    return (int) file->wav.sampleRate;
}

/** @returns the total length of the file, in PCM frames (== samples, files are mono). */
uint64_t audio_total_frames(AudioFile* file) {
    return file->wav.totalPCMFrameCount;
}

/** @returns the current read position in the file, in PCM frames (== samples, files are mono).
 * After looping back at EOF, the position reflects the new pass through the file. */
uint64_t audio_position_frames(AudioFile* file) {
    return file->wav.readCursorInPCMFrames;
}
