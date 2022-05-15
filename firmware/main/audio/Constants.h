#ifndef RADIO__AUDIO__CONSTANTS_H
#define RADIO__AUDIO__CONSTANTS_H

#define I2S_TIMER_INTERVAL_MS (20)
#define I2S_NUM_SAMPLES_PER_INTERVAL (160)

namespace sm1000neo::audio
{
    enum ChannelLabel 
    { 
        LEFT_CHANNEL, 
        RIGHT_CHANNEL,
        USER_CHANNEL = LEFT_CHANNEL,
        RADIO_CHANNEL = RIGHT_CHANNEL,
    };
}

#endif // RADIO__AUDIO__CONSTANTS_H