#include "GenericPTT.h"
#include "smooth/core/task_priorities.h"

using namespace smooth;
using namespace smooth::core;
using namespace smooth::core::ipc;
using namespace std::chrono;

namespace sm1000neo::radio::ptt
{

GenericPTT::GenericPTT()
    : core::Task("PTTTask", 4096, 10, milliseconds(1)),
      queue_(PTTQueue::create(5, *this, *this))
{
    // empty
}

void GenericPTT::event(const PTTRequest& event)
{
    // Called when we switch over to this task after a PTT request was queued.
    setPTT_(event.pttState);
}

void GenericPTT::queuePTT(bool pttState)
{
    // Pushes request onto the queue. The PTT task will be notified
    // once this finishes to actually enable/disable PTT.
    auto q = queue_.lock();
    if (q)
    {
        PTTRequest req(pttState);
        q->push(req);
    }
}

}