#include "audio.h"
#include <stdio.h>
#include <math.h>
#include <float.h>

/** Opens an audio file for reading.
 *
 * @returns 0 if it fails to open the file, or if the format is not 16KHZ Mono, or a handle to the sound file if successful.
 */
SNDFILE* audio_open(const char* file_path) {
    SF_INFO info;
    info.format = 0;
    SNDFILE* file = sf_open(file_path, SFM_READ, &info);
    if (file == NULL) {
        printf("Failed to open file.\n");
        return 0;
    }

    printf("Audio file opened: channels %d, sample rate %d\n", info.channels, info.samplerate);
    printf("  Total frames: %ld (duration: %.2f seconds)\n", info.frames, (double)info.frames / info.samplerate);
    if (info.channels != 1 || info.samplerate != 16000) {
        printf("Format not compatible. Can only accept single channel 16KHz files.\n");
        return 0;
    }
    return file;
}

/** Reads a chunk of audio from the sound file
 *
 * @param file: pointer to a sound file handle from audio_open()
 * @param file_path: path to the audio file (used for reopening on loop)
 * @buffer buffer: the audio buffer into which to save the audio
 * @amount amount: the amount of samples (uint16_t items) to read into the buffer
 * @returns 1 if successful, 0 if an error occurred
 */
int audio_read(SNDFILE** file, const char* file_path, int16_t* buffer, size_t amount) {
    sf_count_t read = sf_read_short(*file, buffer, amount);
    if (read != amount) {
        sf_close(*file);
        
        SF_INFO info;
        info.format = 0;
        *file = sf_open(file_path, SFM_READ, &info);
        if (*file == NULL) {
            printf("Error: Failed to reopen file for looping.\n");
            return 0;
        }
        printf("Looping back to the start of the audio file.\n");
        
        size_t remaining = amount - read;
        int16_t* buffer_offset = buffer + read;
        sf_count_t read_remaining = sf_read_short(*file, buffer_offset, remaining);
        if (read_remaining != remaining) {
            printf("Error: File too small to fill a single buffer. Cannot loop.\n");
            return 0;
        }
    }
    return 1;
}

/** Calculates the RMS of a block of audio
 * This implements the threshold calculation formula as described in the documentation.
 *
 * @param buffer: the audio buffer
 * @param size: the size of the buffer
 * @returns the RMS of the block of audio, in decibels
 */
int audio_calculate_rms(int16_t* buffer, size_t size) {
    double sum;

    for (int i = 0; i < size; i++) {
        double value = ((double) buffer[i]) / 32768;
        value = value * value;
        sum += value;
    }

    double mean = sum / size;
    double rms = sqrtf(mean);
    double rms_db = 20 * log10f(rms + FLT_EPSILON);
    return rms_db;
}
