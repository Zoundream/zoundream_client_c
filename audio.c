#include "audio.h"
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
        fprintf(stderr, "Failed to open file.\n");
        return 0;
    }

    printf("Audio file opened: channels %d, sample rate %d\n", info.channels, info.samplerate);
    if (info.channels != 1 || info.samplerate != 16000) {
        fprintf(stderr, "Format not compatible. Can only accept single channel 16KHz files.\n");
        return 0;
    }
    return file;
}

/** Reads a chunk of audio from the sound file
 *
 * @param file: a handle to a sound file from audio_open()
 * @buffer buffer: the audio buffer into which to save the audio
 * @amount amount: the amount of samples (uint16_t items) to read into the buffer
 * @returns 1 if successful, 0 if the end of the file was reached or another error occurred
 */
int audio_read(SNDFILE* file, int16_t* buffer, size_t amount) {
    sf_count_t read = sf_read_short(file, buffer, amount);
    if (read != amount) {
        fprintf(stderr, "Finished reading file. Exiting.\n");
        return 0;
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
