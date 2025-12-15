#include "internal.h"

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
			cfg.kafka.host = j["kafka"]["host"];
			cfg.kafka.port = j["kafka"]["port"];

			cfg.redis.host = j["redis"]["host"];
			cfg.redis.port = j["redis"]["port"];

			cfg.mysql.host = j["mysql"]["host"];
			cfg.mysql.port = j["mysql"]["port"];
			cfg.mysql.user = j["mysql"]["user"];
			cfg.mysql.password = j["mysql"]["password"];

			cfg.consul.host = j["consul"]["host"];
			cfg.consul.port = j["consul"]["port"];
			cfg.consul.account_srv.host = j["consul"]["account_srv"]["host"];
			cfg.consul.file_srv.host = j["consul"]["file_srv"]["host"];
			cfg.consul.gateway_srv.host = j["consul"]["gateway_srv"]["host"];

			cfg.jwt.secret = j["jwt"]["signing_key"];

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
	props[PropertyKeyConst::NAMESPACE] = "ce99961c-0fcf-4f4f-81d6-ac2183f24df1";
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
	configSvc->addListener("clouddisk.json", "dev", listener);

	// 5. 获取初始配置
	NacosString content;
	try
	{
		content = configSvc->getConfig("clouddisk.json", "dev", 5000);
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

// 获取未占用的port
int GetFreePort()
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		return -1;

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = 0; // 让 OS 自动选择可用端口

	if (bind(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
	{
		close(sock);
		return -1;
	}

	socklen_t len = sizeof(addr);
	if (getsockname(sock, (sockaddr *)&addr, &len) == -1)
	{
		close(sock);
		return -1;
	}

	int port = ntohs(addr.sin_port);
	close(sock);
	return port;
}
