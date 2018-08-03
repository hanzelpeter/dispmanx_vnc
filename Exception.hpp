#pragma once

#include <exception>
#include <string>

class Exception : public std::exception
{
public:
	Exception() {
	}

	Exception(const std::string& whatString)
		: m_whatString(whatString) {
	};

	const char *what() const noexcept override {
		return m_whatString.c_str();
	}

private:
	std::string m_whatString;
};

class HelpException : public Exception
{
};

class ParamException : public Exception
{
};
