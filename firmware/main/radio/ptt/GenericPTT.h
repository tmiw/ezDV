#ifndef SM1000_RADIO_PTT_GENERIC_H
#define SM1000_RADIO_PTT_GENERIC_H

#include "smooth/core/Task.h"
#include "smooth/core/ipc/TaskEventQueue.h"

namespace sm1000neo::radio::ptt
{    
    struct PTTRequest
    {
        PTTRequest(bool state = false)
            : pttState(state)
        {
            // empty
        }
        
        bool pttState;
    };
    
    using PTTQueue = smooth::core::ipc::TaskEventQueue<PTTRequest>;
    
    class GenericPTT : 
        public smooth::core::Task,
        public smooth::core::ipc::IEventListener<PTTRequest>
    {
    public:
        void event(const PTTRequest& event) override;
        
        void queuePTT(bool pttState);
        
    protected:
        GenericPTT();
        
        virtual void init() = 0;
        virtual void setPTT_(bool pttState) = 0;
        
    private:
        std::weak_ptr<PTTQueue> queue_;
    };

}

#endif // SM1000_RADIO_PTT_GENERIC_H