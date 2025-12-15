#ifndef GLOBAL_LOGGER_H
#define GLOBAL_LOGGER_H

#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <iostream>
class GlobalLogger
{
public:
	// 获取全局 logger（线程安全，C++11 保证）
	static std::shared_ptr<spdlog::logger> get();

	// 初始化（可调用一次，不调用则使用默认）
	static void init(const std::string &filename = "/home/lihaoqian/myproject/clouddisk_v2/forward_part/logs/app.log",
					 size_t max_size = 10 * 1024 * 1024,
					 size_t max_files = 3,
					 bool async_mode = false);

private:
	GlobalLogger() = default;
	static std::shared_ptr<spdlog::logger> logger_;
};

// ------ 全局宏，和 glog 一样使用 ------
#define LOG_INFO(...) SPDLOG_LOGGER_INFO(GlobalLogger::get(), __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(GlobalLogger::get(), __VA_ARGS__)
#define LOG_WARN(...) SPDLOG_LOGGER_WARN(GlobalLogger::get(), __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(GlobalLogger::get(), __VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(GlobalLogger::get(), __VA_ARGS__)

#endif // GLOBAL_LOGGER_H
