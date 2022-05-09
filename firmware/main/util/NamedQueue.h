#ifndef UTIL__NAMED_QUEUE_H
#define UTIL__NAMED_QUEUE_H

#include <map>
#include <string>
#include <mutex>
#include "smooth/core/ipc/TaskEventQueue.h"

namespace sm1000neo::util
{
    class NamedQueue
    {
    public:
        template<typename T>
        static void Add(std::string name, std::shared_ptr<smooth::core::ipc::TaskEventQueue<T>> queue);
        
        template<typename T>
        static bool Send(std::string name, T& message);
        
    private:
        static std::map<std::string, void*> NamedQueueMap_;
        static std::mutex NamedQueueMutex_;
        
        template<typename T>
        static std::weak_ptr<smooth::core::ipc::TaskEventQueue<T>> Get_(std::string name);
    };
    
    template<typename T>
    void NamedQueue::Add(std::string name, std::shared_ptr<smooth::core::ipc::TaskEventQueue<T>> queue)
    {
        std::unique_lock<std::mutex> lock(NamedQueueMutex_);
        
        auto ptr = new std::weak_ptr<smooth::core::ipc::TaskEventQueue<T>>(queue);
        assert(ptr != nullptr);
        
        NamedQueueMap_[name] = (void*)ptr;
    }
    
    template<typename T>
    bool NamedQueue::Send(std::string name, T& message)
    {
        if (NamedQueueMap_.find(name) == NamedQueueMap_.end()) return false;
        
        auto queue = Get_<T>(name);
        auto lck = queue.lock();
        if (lck)
        {
            lck->push(message);
            return true;
        }
        return false;
    }
    
    template<typename T>
    std::weak_ptr<smooth::core::ipc::TaskEventQueue<T>> NamedQueue::Get_(std::string name)
    {
        std::unique_lock<std::mutex> lock(NamedQueueMutex_);
        return *(std::weak_ptr<smooth::core::ipc::TaskEventQueue<T>>*)NamedQueueMap_[name];
    }
}

#endif // UTIL__NAMED_QUEUE_H