#ifndef INTERNAL_H
#define INTERNAL_H

#pragma once
#include <string>
#include "Nacos.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include "../logs/Logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
struct ServerConfig
{
	std::string host;
};

struct RedisConfig
{
	std::string host;
	std::string port;
};

struct MysqlConfig
{
	std::string host;
	std::string port;
	std::string user;
	std::string password;
};

struct ConsulConfig
{
	std::string host;
	std::string port;
	ServerConfig account_srv;
	ServerConfig file_srv;
	ServerConfig gateway_srv;
};

struct JWTConfig
{
	std::string secret;
};

class AppConfig
{
public:
	RedisConfig redis;
	MysqlConfig mysql;
	ConsulConfig consul;
	JWTConfig jwt;

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
// 获取未占用的port
int GetFreePort();
#endif // !INTERNAL_H