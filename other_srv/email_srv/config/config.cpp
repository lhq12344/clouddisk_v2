#include "config.h"

using json = nlohmann::json;
using namespace nacos;

class ConfigListener : public Listener
{
public:
	void receiveConfigInfo(const std::string &configInfo)
	{
		std::cout << "[Nacos] Config updated:\n"
				  << configInfo << std::endl;
		LoadConfigFromString(configInfo);
	}

	static void LoadConfigFromString(const std::string &content)
	{
		try
		{
			auto j = json::parse(content);

			AppConfig &cfg = AppConfig::getInstance();
			cfg.kafka.brokers = j["kafka"]["brokers"];
			cfg.kafka.topic = j["kafka"]["topic"];
			cfg.kafka.group_id = j["kafka"]["group_id"];

			cfg.redis.host = j["redis"]["host"];
			cfg.redis.port = j["redis"]["port"];

			cfg.smtp.url = j["smtp"]["url"];
			cfg.smtp.user = j["smtp"]["user"];
			cfg.smtp.pass = j["smtp"]["pass"];
			cfg.smtp.from = j["smtp"]["from"];
			cfg.smtp.from_name = j["smtp"]["from_name"];

			cfg.email.code_ttl_sec = j["email"]["code_ttl_sec"];
			cfg.email.dedup_ttl_sec = j["email"]["dedup_ttl_sec"];

			std::cout << "[Nacos] Config parsed successfully\n";
		}
		catch (std::exception &e)
		{
			std::cerr << "[ERROR] Failed to parse config: " << e.what() << std::endl;
		}
	}
};

void InitAppConfig()
{

	// 1. 创建 Properties
	Properties props;
	props[PropertyKeyConst::SERVER_ADDR] = "192.168.149.128:30848";
	props[PropertyKeyConst::NAMESPACE] = "0d83a21b-0039-4f50-9e53-4a7fd3e5d22e";
	props[PropertyKeyConst::AUTH_USERNAME] = "nacos";
	props[PropertyKeyConst::AUTH_PASSWORD] = "nacos";
	// 2. 正确的工厂（旧版 API）
	INacosServiceFactory *factory = NacosFactoryFactory::getNacosFactory(props);
	ResourceGuard<INacosServiceFactory> guardFactory(factory);

	// 3. 创建 ConfigService
	ConfigService *configSvc = factory->CreateConfigService();
	ResourceGuard<ConfigService> guardConfig(configSvc);

	// 4. 监听配置
	ConfigListener *listener = new ConfigListener();
	configSvc->addListener("email_config.json", "dev", listener);

	// 5. 获取初始配置
	NacosString content;
	try
	{
		content = configSvc->getConfig("email_config.json", "dev", 5000);
	}
	catch (NacosException &e)
	{
		std::cerr << "[ERROR] getConfig failed: "
				  << e.errorcode() << " " << e.what() << std::endl;
		LOG_ERROR("[ERROR] getConfig failed: ", e.errorcode(), e.what());
		return;
	}

	if (content.empty())
	{
		std::cerr << "[ERROR] empty config!" << std::endl;
		LOG_ERROR("[ERROR] empty config!");
		return;
	}

	std::cout << "[Nacos] Initial config:\n"
			  << content << std::endl;

	ConfigListener::LoadConfigFromString(content);
}