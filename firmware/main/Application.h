#ifndef SM1000_APP_H
#define SM1000_APP_H

#include "smooth/core/Application.h"
#include "audio/TLV320.h"
#include "codec/FreeDVTask.h"
#include "radio/ptt/GpioPTT.h"

namespace sm1000neo
{
    class App : public smooth::core::Application
    {
    public:
        App();
        
        void init() override;
        
    private:
        radio::ptt::GpioPTT ptt;
        audio::TLV320 soundCodec;
        codec::FreeDVTask freedvTask;
    };
}

#endif // SM1000_APP_H