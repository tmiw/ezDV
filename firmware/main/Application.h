#ifndef SM1000_APP_H
#define SM1000_APP_H

#include "smooth/core/Application.h"
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
    };
}

#endif // SM1000_APP_H