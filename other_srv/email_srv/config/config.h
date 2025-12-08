#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include "Nacos.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include "../log/Logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
using  std::string;

struct SmtpConfig
{
    string url;
    string user;
    string pass;
    string from;
    string from_name;
};

struct RedisConfig
{
    string host;
    int port;
};

struct KafkaConfig
{
    string brokers;
    string topic;
    string group_id;
};

struct EmailBizConfig
{
    int code_ttl_sec;
    int dedup_ttl_sec;
};

class AppConfig
{
public:
	SmtpConfig smtp;
    RedisConfig redis;
    KafkaConfig kafka;
    EmailBizConfig email;
    int worker_threads;
	/// ---- 单例全局访问接口 ----
	static AppConfig &getInstance()
	{
		static AppConfig instance; // C++11 线程安全
		return instance;
	}

private:
	AppConfig() = default;
	~AppConfig() = default;

	AppConfig(const AppConfig &) = delete;
	AppConfig &operator=(const AppConfig &) = delete;
};
void InitAppConfig(void);

#endif // !CONFIG_H