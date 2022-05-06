#ifndef SM1000_APP_H
#define SM1000_APP_H

#include "smooth/core/Application.h"

namespace sm1000neo
{
    class App : public smooth::core::Application
    {
    public:
        App();
        
        void init() override;
    };
}

#endif // SM1000_APP_H