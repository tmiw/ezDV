#ifndef SM1000_RADIO_GPIO_PTT_H
#define SM1000_RADIO_GPIO_PTT_H

#include "GenericPTT.h"

namespace sm1000neo::radio::ptt
{
    class GpioPTT : public GenericPTT
    {
    public:
        GpioPTT();
        
    protected:
        virtual void init();
        virtual void setPTT_(bool pttState);
    };
}

#endif // SM1000_RADIO_GPIO_PTT_H