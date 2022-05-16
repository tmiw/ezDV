#ifndef SM1000_APP_H
#define SM1000_APP_H

#include "smooth/core/Application.h"
#include "audio/TLV320.h"
#include "codec/FreeDVTask.h"
#include "radio/icom/IcomRadioTask.h"
#include "ui/UserInterfaceTask.h"
#include "audio/AudioMixer.h"

namespace sm1000neo
{
    class App : public smooth::core::Application
    {
    public:
        App();
        
        void init() override;
        
    private:
        ui::UserInterfaceTask uiTask;
        //radio::icom::IcomRadioTask radioTask;
    };
}

#endif // SM1000_APP_H