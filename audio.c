#include "audio.h"
#include "zc_log.h"
#include <stdio.h>
#include <string.h>

/** Opens an audio file for reading.
 *
 * @returns 0 if it fails to open the file, or if the format is not 16KHZ Mono, or a handle to the sound file if successful.
 */
SNDFILE* audio_open(const char* file_path) {
    SF_INFO info;
    info.format = 0;
    SNDFILE* file = sf_open(file_path, SFM_READ, &info);
    if (file == NULL) {
        zc_log("Failed to open file.");
        return 0;
    }

    zc_log("Audio file opened: channels %d, sample rate %d", info.channels, info.samplerate);
    zc_log("  Total frames: %ld (duration: %.2f seconds)", info.frames, (double)info.frames / info.samplerate);
    if (info.channels != 1 || info.samplerate != 16000) {
        zc_log("Format not compatible. Can only accept single channel 16KHz files.");
        sf_close(file);
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
int audio_read(SNDFILE* file, int16_t* buffer, size_t amount, int loop_on_eof, int* reached_eof) {
    *reached_eof = 0;
    sf_count_t read = sf_read_short(file, buffer, amount);
    if (read < 0) {
        zc_log("Error: failed to read from the audio file.");
        return 0;
    }
    if ((size_t) read < amount) {
        // Reached the end of the file before filling the buffer.
        *reached_eof = 1;
        size_t filled = (size_t) read;
        if (loop_on_eof) {
            // Loop the file: rewind and keep filling from the start so the stream stays continuous.
            sf_seek(file, 0, SEEK_SET);
            sf_count_t more = sf_read_short(file, buffer + filled, amount - filled);
            if (more > 0) filled += (size_t) more;
        }
        if (filled < amount) {
            // Not looping (digital silence), or the file is shorter than one buffer: zero the rest.
            memset(buffer + filled, 0, (amount - filled) * sizeof(int16_t));
        }
    }
    return 1;
}
