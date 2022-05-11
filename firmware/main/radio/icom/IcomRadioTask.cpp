#include "IcomRadioTask.h"

namespace sm1000neo::radio::icom
{
    void IcomRadioTask::init()
    {
        controlChannelSM_.start("10.1.2.3", 50001, "YOUR_USERNAME", "YOUR_PASSWORD");
    }
}
