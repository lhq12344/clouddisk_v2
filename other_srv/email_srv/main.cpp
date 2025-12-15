#include "KafkaConsumer.h"
#include <signal.h>

void signal_handler(int sig)
{
    std::cout << "Signal " << sig << " received, exiting..." << std::endl;
    g_running = false;
}


int main(int argc, char *argv[])
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

	InitAppConfig();

    // curl 全局初始化/清理
    curl_global_init(CURL_GLOBAL_DEFAULT);

    try
    {
        EmailService emailSvc(AppConfig::getInstance());
        KafkaConsumer consumer(AppConfig::getInstance().kafka, emailSvc);

        std::cout << "Email service started. Listening Kafka topic: "
             << AppConfig::getInstance().kafka.topic << std::endl;
        consumer.loop();
    }
    catch (const std::exception &e)
    {
         std::cerr << "Fatal error: " << e.what() <<  std::endl;
        curl_global_cleanup();
        return 2;
    }

    curl_global_cleanup();
    return 0;
}