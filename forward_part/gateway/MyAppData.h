#pragma once
#include <drogon/orm/DbClient.h>
#include <memory>
#include <string>
class MyAppData
{
public:
    std::string kafkaHost;
	int kafkaPort{0};
	std::string consulHost;
	int consulPort{0};
	std::string SigningKey;
	drogon::orm::DbClientPtr mysqlClient;
	static MyAppData &instance()
	{
		static MyAppData d;
		return d;
	}

private:
	MyAppData() = default;
};
