#ifndef RADIO__AUDIO__MESSAGING_H
#define RADIO__AUDIO__MESSAGING_H

#define FREEDV_AUDIO_IN_PIPE_NAME "FreeDVTask_AudioInput"
#define AUDIO_OUT_PIPE_NAME "TLV320_AudioOut"
#define USER_CHANNEL (sm1000neo::audio::AudioDataMessage::LEFT_CHANNEL)
#define RADIO_CHANNEL (sm1000neo::audio::AudioDataMessage::RIGHT_CHANNEL)

#define NUM_SAMPLES_PER_AUDIO_MESSAGE (80) /* 8000 Hz * 10ms */

namespace sm1000neo::audio
{
    struct AudioDataMessage
    {
        enum ChannelLabel { LEFT_CHANNEL, RIGHT_CHANNEL } channel;
        short audioData[NUM_SAMPLES_PER_AUDIO_MESSAGE];
    };
}

#endif // RADIO__AUDIO__MESSAGING_H