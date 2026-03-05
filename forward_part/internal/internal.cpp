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
	try
	{
		// 使用本地配置文件（避免 Nacos C++ SDK 兼容性问题）
		std::string configFile = "config.json";

		// 尝试多个可能的配置文件路径
		std::vector<std::string> possiblePaths = {
			"config.json",
			"../config.json",
			"/home/lihaoqian/project/clouddisk_v2/forward_part/gateway/config.json"
		};

		std::string configContent;
		bool found = false;

		for (const auto& path : possiblePaths) {
			std::ifstream file(path);
			if (file.is_open()) {
				std::stringstream buffer;
				buffer << file.rdbuf();
				configContent = buffer.str();
				found = true;
				std::cout << "[Config] Loaded from: " << path << std::endl;
				break;
			}
		}

		if (!found) {
			std::cerr << "[ERROR] Config file not found!" << std::endl;
			LOG_ERROR("[ERROR] Config file not found!");
			return;
		}

		if (configContent.empty())
		{
			std::cerr << "[ERROR] empty config!" << std::endl;
			LOG_ERROR("[ERROR] empty config!");
			return;
		}

		std::cout << "[Config] Loaded successfully" << std::endl;
		ConfigListener::LoadConfigFromString(configContent);
	}
	catch (std::exception &e)
	{
		std::cerr << "[FATAL] InitAppConfig exception: " << e.what() << std::endl;
		LOG_ERROR("[FATAL] InitAppConfig exception: ", e.what());
	}
	catch (...)
	{
		std::cerr << "[FATAL] InitAppConfig unknown exception" << std::endl;
		LOG_ERROR("[FATAL] InitAppConfig unknown exception");
	}
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
