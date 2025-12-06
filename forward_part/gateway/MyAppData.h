#pragma once
#include <memory>
#include <string>
class MyAppData
{
public:
	std::string consulHost;
	int consulPort;
	static MyAppData &instance()
	{
		static MyAppData d;
		return d;
	}

private:
	MyAppData() = default;
};
