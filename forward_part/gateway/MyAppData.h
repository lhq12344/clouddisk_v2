#pragma once
#include <memory>
#include <string>
class MyAppData
{
public:
    std::string kafkaHost;
	int kafkaPort;
	std::string consulHost;
	int consulPort;
	std::string SigningKey;
	static MyAppData &instance()
	{
		static MyAppData d;
		return d;
	}

private:
	MyAppData() = default;
};
