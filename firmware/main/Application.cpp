#include "Application.h"
#include "smooth/core/task_priorities.h"
#include "smooth/core/SystemStatistics.h"

#include "audio/TLV320.h"
#include "codec/FreeDVTask.h"
#include "radio/icom/IcomRadioTask.h"
#include "audio/AudioMixer.h"

using namespace smooth;
using namespace smooth::core;

namespace ezdv
{
    App::App()
        : Application(APPLICATION_BASE_PRIO, std::chrono::milliseconds(1000))
    {
        // empty
    }

    void App::init()
    {
        Application::init();
        
        ezdv::audio::TLV320::ThisTask().start();
        ezdv::codec::FreeDVTask::ThisTask().start();
        ezdv::audio::AudioMixer::ThisTask().start();
        uiTask.start();

#if 0    
        smooth::core::network::Wifi& wifi = get_wifi();
        wifi.set_host_name("ezdv");
        wifi.set_auto_connect(true);
        wifi.set_ap_credentials("YOUR WIFI NETWORK", "YOUR WIFI PASSWORD");
        wifi.connect_to_ap();
        
        vTaskDelay(pdMS_TO_TICKS(10000));
        auto& radioTask = ezdv::radio::icom::IcomRadioTask::ThisTask();
        radioTask.setLocalIp(wifi.get_local_ip());
        radioTask.setAuthInfo("192.168.59.1", 50001, "YOUR RADIO USERNAME", "YOUR RADIO PASSWORD");
        radioTask.start();
#endif
    }
}

extern "C"
{
    
#ifdef ESP_PLATFORM
void app_main()
{
    ezdv::App app{};
    app.start();
}
#else
int main(int /*argc*/, char** /*argv*/)
{
    smooth::core::SystemStatistics::instance().dump();
    ezdv::App app{};
    app.start();
    return 0;
}
#endif

}
