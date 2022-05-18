#include "NamedQueue.h"

namespace ezdv::util
{
    std::map<std::string, void*> NamedQueue::NamedQueueMap_;
    std::mutex NamedQueue::NamedQueueMutex_;
}