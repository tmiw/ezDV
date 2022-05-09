#ifndef CODEC__MESSAGING_H
#define CODEC__MESSAGING_H

#define FREEDV_CONTROL_PIPE_NAME "FreeDVTask_Control"
#define FREEDV_PTT_PIPE_NAME "FreeDVTask_PTT"

namespace sm1000neo::codec
{
    struct FreeDVChangeModeMessage
    {
        int newMode;
    };
    
    struct FreeDVChangePTTMessage
    {
        bool pttEnabled;
    };
}

#endif // CODEC__MESSAGING_H