#include "Logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>

std::shared_ptr<spdlog::logger> GlobalLogger::logger_ = nullptr;

void GlobalLogger::init(const std::string &filename,
						size_t max_size,
						size_t max_files,
						bool async_mode)
{
	if (logger_)
		return; // 防止重复初始化

	try
	{
		// 控制台输出
		auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l] %v");

		// 滚动日志文件
		auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
			filename, max_size, max_files);
		file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l] %v");

		std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};

		if (async_mode)
		{
			// 异步日志队列（默认 8192 条）
			spdlog::init_thread_pool(8192, 1);
			logger_ = std::make_shared<spdlog::async_logger>(
				"global_logger",
				sinks.begin(),
				sinks.end(),
				spdlog::thread_pool(),
				spdlog::async_overflow_policy::block);
		}
		else
		{
			logger_ = std::make_shared<spdlog::logger>(
				"global_logger",
				sinks.begin(),
				sinks.end());
		}

		spdlog::register_logger(logger_);
		logger_->set_level(spdlog::level::debug);
		logger_->flush_on(spdlog::level::info);
	}
	catch (const spdlog::spdlog_ex &ex)
	{
		std::cerr << "Log init failed: " << ex.what() << std::endl;
		throw;
	}
}

std::shared_ptr<spdlog::logger> GlobalLogger::get()
{
	if (!logger_)
	{
		init(); // 默认初始化
	}
	return logger_;
}
