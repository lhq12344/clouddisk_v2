#include "config.h"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <sstream>

using json = nlohmann::json;
using namespace nacos;

namespace
{
std::string envString(const char *name)
{
	const char *value = std::getenv(name);
	return value == nullptr ? "" : std::string(value);
}

void applyEnvironmentOverrides(AppConfig &cfg)
{
	const auto smtpURL = envString("EMAIL_SMTP_URL");
	const auto smtpUser = envString("EMAIL_SMTP_USER");
	const auto smtpPass = envString("EMAIL_SMTP_PASS");
	const auto smtpFrom = envString("EMAIL_SMTP_FROM");
	const auto smtpFromName = envString("EMAIL_SMTP_FROM_NAME");

	if (!smtpURL.empty())
		cfg.smtp.url = smtpURL;
	if (!smtpUser.empty())
		cfg.smtp.user = smtpUser;
	if (!smtpPass.empty())
		cfg.smtp.pass = smtpPass;
	if (!smtpFrom.empty())
		cfg.smtp.from = smtpFrom;
	if (!smtpFromName.empty())
		cfg.smtp.from_name = smtpFromName;
}
}

class ConfigListener : public Listener
{
public:
	void receiveConfigInfo(const std::string &configInfo)
	{
		std::cout << "[Nacos] Config updated (bytes=" << configInfo.size() << ")" << std::endl;
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

			applyEnvironmentOverrides(cfg);

			std::cout << "[Nacos] Config parsed successfully\n";
		}
		catch (std::exception &e)
		{
			std::cerr << "[ERROR] Failed to parse config: " << e.what() << std::endl;
		}
	}
};

static void LoadConfigFromFile(const std::string &path)
{
	std::ifstream fin(path);
	if (!fin.is_open())
	{
		std::cerr << "[ERROR] cannot open local config file: " << path << std::endl;
		return;
	}
	std::stringstream buf;
	buf << fin.rdbuf();
	ConfigListener::LoadConfigFromString(buf.str());
}

void InitAppConfig()
{
	// 尝试从 Nacos 加载，失败则回退到本地文件
	NacosString content;
	bool nacosOk = false;

	try
	{
		Properties props;
		props[PropertyKeyConst::SERVER_ADDR] = "127.0.0.1:30848";
		props[PropertyKeyConst::NAMESPACE] = "ce99961c-0fcf-4f4f-81d6-ac2183f24df1";
		props[PropertyKeyConst::AUTH_USERNAME] = "nacos";
		props[PropertyKeyConst::AUTH_PASSWORD] = "nacos";

		INacosServiceFactory *factory = NacosFactoryFactory::getNacosFactory(props);
		ResourceGuard<INacosServiceFactory> guardFactory(factory);

		ConfigService *configSvc = factory->CreateConfigService();
		ResourceGuard<ConfigService> guardConfig(configSvc);

		ConfigListener *listener = new ConfigListener();
		configSvc->addListener("email_config.json", "dev", listener);

		content = configSvc->getConfig("email_config.json", "dev", 5000);
		if (!content.empty())
		{
			std::cout << "[Nacos] Initial config loaded (bytes=" << content.size() << ")" << std::endl;
			ConfigListener::LoadConfigFromString(content);
			nacosOk = true;
		}
	}
	catch (std::exception &e)
	{
		std::cerr << "[Nacos] init failed, fallback to local config: " << e.what() << std::endl;
	}

	if (!nacosOk)
	{
		std::cout << "[Config] Loading from local email_config.json" << std::endl;
		LoadConfigFromFile("email_config.json");
	}
}
