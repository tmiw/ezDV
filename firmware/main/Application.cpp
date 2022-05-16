#include "Application.h"
#include "smooth/core/task_priorities.h"
#include "smooth/core/SystemStatistics.h"

using namespace smooth;
using namespace smooth::core;

namespace sm1000neo
{
    App::App()
        : Application(APPLICATION_BASE_PRIO, std::chrono::milliseconds(1000))
    {
        // empty
    }

    void App::init()
    {
        Application::init();
        
        sm1000neo::audio::TLV320::ThisTask().start();
        sm1000neo::codec::FreeDVTask::ThisTask().start();
        sm1000neo::audio::AudioMixer::ThisTask().start();
        uiTask.start();
    
#if 0
        smooth::core::network::Wifi& wifi = get_wifi();
        wifi.set_host_name("sm1000neo");
        wifi.set_auto_connect(true);
        wifi.set_ap_credentials("YOUR WIRELESS NETWORK", "YOUR WIRELESS PASSWORD");
        wifi.connect_to_ap();
        
        vTaskDelay(pdMS_TO_TICKS(10000));
        radioTask.setLocalIp(wifi.get_local_ip());
        radioTask.start();
#endif
    }
}

extern "C"
{
    
#ifdef ESP_PLATFORM
void app_main()
{
    sm1000neo::App app{};
    app.start();
}
#else
int main(int /*argc*/, char** /*argv*/)
{
    smooth::core::SystemStatistics::instance().dump();
    sm1000neo::App app{};
    app.start();
    return 0;
}
#endif

}
