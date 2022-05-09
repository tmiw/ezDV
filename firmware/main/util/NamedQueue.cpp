#include "NamedQueue.h"

namespace sm1000neo::util
{
    std::map<std::string, void*> NamedQueue::NamedQueueMap_;
    std::mutex NamedQueue::NamedQueueMutex_;
}