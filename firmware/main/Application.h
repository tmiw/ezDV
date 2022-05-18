#ifndef SM1000_APP_H
#define SM1000_APP_H

#include "smooth/core/Application.h"
#include "ui/UserInterfaceTask.h"

namespace ezdv
{
    class App : public smooth::core::Application
    {
    public:
        App();
        
        void init() override;
        
    private:
        ui::UserInterfaceTask uiTask;
    };
}

#endif // SM1000_APP_H