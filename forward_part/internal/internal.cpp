#ifdef _WIN32
#include <winsock2.h>
#endif

#include "internal.h"

#include <cstdlib>

#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using json = nlohmann::json;
using namespace nacos;

namespace
{
	std::string jsonToString(const json &value)
	{
		if (value.is_string())
		{
			return value.get<std::string>();
		}
		if (value.is_number_integer())
		{
			return std::to_string(value.get<long long>());
		}
		if (value.is_number_unsigned())
		{
			return std::to_string(value.get<unsigned long long>());
		}
		if (value.is_number_float())
		{
			return std::to_string(value.get<double>());
		}
		return value.dump();
	}

	std::string envString(const char *name)
	{
		const char *value = std::getenv(name);
		return value == nullptr ? "" : std::string(value);
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
			cfg.kafka.host = jsonToString(j["kafka"]["host"]);
			cfg.kafka.port = jsonToString(j["kafka"]["port"]);

			cfg.redis.host = jsonToString(j["redis"]["host"]);
			cfg.redis.port = jsonToString(j["redis"]["port"]);

			cfg.mysql.host = jsonToString(j["mysql"]["host"]);
			cfg.mysql.port = jsonToString(j["mysql"]["port"]);
			cfg.mysql.user = jsonToString(j["mysql"]["user"]);
			cfg.mysql.password = jsonToString(j["mysql"]["password"]);
			if (j["mysql"].contains("database"))
			{
				cfg.mysql.database = jsonToString(j["mysql"]["database"]);
			}
			else if (j["mysql"].contains("dbname"))
			{
				cfg.mysql.database = jsonToString(j["mysql"]["dbname"]);
			}
			else
			{
				cfg.mysql.database = "clouddisk";
			}

			cfg.consul.host = jsonToString(j["consul"]["host"]);
			cfg.consul.port = jsonToString(j["consul"]["port"]);
			if (j["consul"].contains("storage_control"))
			{
				cfg.consul.storage_control.host = jsonToString(j["consul"]["storage_control"]["host"]);
				cfg.consul.storage_control.port = jsonToString(j["consul"]["storage_control"]["port"]);
			}
			cfg.consul.gateway_srv.host = jsonToString(j["consul"]["gateway_srv"]["host"]);
			cfg.consul.gateway_srv.port = jsonToString(j["consul"]["gateway_srv"]["port"]);

			cfg.jwt.secret = jsonToString(j["jwt"]["signing_key"]);
			if (cfg.configVersion.empty())
			{
				cfg.configVersion = "local-file";
			}

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
		std::string configContent;
		std::string loadedPath;
		bool found = false;

		const auto envConfigJson = envString("CLOUDDISK_CONFIG_JSON");
		if (!envConfigJson.empty())
		{
			configContent = envConfigJson;
			loadedPath = "env:CLOUDDISK_CONFIG_JSON";
			found = true;
			std::cout << "[Config] Loaded from CLOUDDISK_CONFIG_JSON" << std::endl;
		}

		std::vector<std::string> possiblePaths;
		const auto envConfigFile = envString("CLOUDDISK_CONFIG_FILE");
		if (!envConfigFile.empty())
		{
			possiblePaths.push_back(envConfigFile);
		}
		possiblePaths.push_back("config.json");
		possiblePaths.push_back("../config.json");
		possiblePaths.push_back("/home/lihaoqian/project/clouddisk_v2/forward_part/gateway/config.json");

		if (!found)
		{
			for (const auto &path : possiblePaths)
			{
				std::ifstream file(path);
				if (file.is_open())
				{
					std::stringstream buffer;
					buffer << file.rdbuf();
					configContent = buffer.str();
					loadedPath = path;
					found = true;
					std::cout << "[Config] Loaded from: " << path << std::endl;
					break;
				}
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
		AppConfig::getInstance().configSource = loadedPath;
		const auto envConfigVersion = envString("CLOUDDISK_CONFIG_VERSION");
		AppConfig::getInstance().configVersion = envConfigVersion.empty() ? loadedPath : envConfigVersion;
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
#ifdef _WIN32
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		return -1;
	}

	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
	{
		WSACleanup();
		return -1;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = 0; // 让 OS 自动选择可用端口

	if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR)
	{
		closesocket(sock);
		WSACleanup();
		return -1;
	}

	int len = sizeof(addr);
	if (getsockname(sock, reinterpret_cast<sockaddr *>(&addr), &len) == SOCKET_ERROR)
	{
		closesocket(sock);
		WSACleanup();
		return -1;
	}

	int port = ntohs(addr.sin_port);
	closesocket(sock);
	WSACleanup();
	return port;
#else
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
#endif
}
