#ifndef RADIO__MESSAGING_H
#define RADIO__MESSAGING_H

#define RADIO_CONTROL_PIPE_NAME "Radio_Control"

namespace ezdv::radio
{
    struct RadioPTTMessage
    {
        bool value;
    };
}

#endif // RADIO__MESSAGING_H