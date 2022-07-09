#ifndef AUDIO__MESSAGING_H
#define AUDIO__MESSAGING_H

#include "Constants.h"

#define TLV320_CONTROL_PIPE_NAME "TLV320_Control"

namespace ezdv::audio
{
    struct ChangeVolumeMessage
    {
        ChannelLabel channel;
        bool direction; // 0 = down, 1 = up
    };
}

#endif // AUDIO__MESSAGING_H