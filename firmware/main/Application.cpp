#include "Application.h"
#include "smooth/core/task_priorities.h"

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