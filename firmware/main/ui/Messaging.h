#ifndef UI__MESSAGING_H
#define UI__MESSAGING_H

#define UI_CONTROL_PIPE_NAME "UI_Control"

namespace ezdv::ui
{
    struct UserInterfaceControlMessage
    {
        enum { UPDATE_SYNC } action;
        bool value;
    };
}

#endif // RADIO__AUDIO__MESSAGING_H