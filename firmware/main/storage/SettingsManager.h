#ifndef STORAGE__SETTINGS_MANAGER_H
#define STORAGE__SETTINGS_MANAGER_H

#include <thread>

#include "smooth/core/Task.h"
#include "smooth/core/util/FixedBuffer.h"
#include "smooth/core/timer/Timer.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.hpp"

namespace ezdv::storage
{
    class SettingsManager : 
        public smooth::core::Task,
        public smooth::core::ipc::IEventListener<smooth::core::timer::TimerExpiredEvent>
    {
    public:
        SettingsManager()
            : smooth::core::Task("SettingsManager", 4096, 10, std::chrono::milliseconds(10))
            , commitQueue_(smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>::create(2, *this, *this))
        {
            // empty
        }
                
        static SettingsManager& ThisTask()
        {
            return Task_;
        }
        
        void event(const smooth::core::timer::TimerExpiredEvent& event) override;
        
        int8_t getLeftChannelVolume();
        int8_t getRightChannelVolume();
        void setLeftChannelVolume(int8_t vol);
        void setRightChannelVolume(int8_t vol);
        
    protected:
        virtual void init() override;
                
    private:
        static SettingsManager Task_;
        
        std::mutex storageMutex_;
        
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>> commitQueue_;
        smooth::core::timer::TimerOwner commitTimer_;
        std::shared_ptr<nvs::NVSHandle> storageHandle_;
        
        int8_t leftChannelVolume_;
        int8_t rightChannelVolume_;
    };

}

#endif // STORAGE__SETTINGS_MANAGER_H
