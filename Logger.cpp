#include "Logger.hpp"

std::mutex Logger::m_mutex{};
std::string Logger::m_defaultModule{};
