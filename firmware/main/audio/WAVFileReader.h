/*
 * File adapted from esp32_sdcard_audio project for use with ezDV. See
 * https://github.com/atomic14/esp32_sdcard_audio/tree/main/idf-wav-sdcard/lib/wav_file/src
 * for original source.
 */

#ifndef WAV_FILE_READER_H
#define WAV_FILE_READER_H

#include "WAVFile.h"
#include <stdio.h>

namespace ezdv
{

namespace audio
{

class WAVFileReader
{
private:
    wav_header_t m_wav_header;

    FILE *m_fp;

public:
    WAVFileReader(FILE *fp);
    int sample_rate() { return m_wav_header.sample_rate; }
    int num_channels() { return m_wav_header.num_channels; }
    int read(int16_t *samples, int count);
};

}

}

#endif // WAV_FILE_READER_H