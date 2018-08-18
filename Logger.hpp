#pragma once

#include <iostream>
#include <sstream>
#include <mutex>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>

class Logger
{
public:
	static Logger Get(const std::string& module = "")
	{
		return {module};
	}
	
	static void SetDefaultModule(const std::string& module)
	{
		m_defaultModule = module;
	}

	~Logger()
	{
		std::lock_guard<std::mutex> lock{m_mutex};
		
		auto now = std::chrono::system_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
		auto tmb = std::chrono::system_clock::to_time_t(now);
		auto bt = *std::localtime(&tmb);
		
#if defined(__GNUC__) && __GNUC__ < 5
		{
			char timeBuff[100];
			std::strftime(timeBuff, sizeof(timeBuff), "%F %X", &bt);
			std::cerr << timeBuff << "." << std::setfill('0') << std::setw(3) << ms.count() << std::setw(0)
				<< " [" << (m_module.empty() ? m_defaultModule : m_module) << "] "
				<< m_stream.str() << std::endl;
		}
#else
		std::cerr << std::put_time(&bt, "%F %X") << "." << std::setfill('0') << std::setw(3) << ms.count() << std::setw(0)
			<< " [" << (m_module.empty() ? m_defaultModule : m_module) << "] "
			<< m_stream.str() << std::endl;
#endif
	}

	template <typename T>
	const Logger& operator <<(T const& value) const
	{
		m_stream << value;
		return *this;
	}

private:
	Logger() = default;
	Logger(const std::string& module)
		: m_module{module}
	{}

	std::string m_module;
	mutable std::stringstream m_stream;

	static std::string m_defaultModule;
	static std::mutex m_mutex;
};
