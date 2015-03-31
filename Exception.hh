#ifndef EXCEPTION_HH__
#define EXCEPTION_HH__

#include <exception>

class Exception : public std::exception
{
public:
	Exception(const char *whatString)
		: m_whatString(whatString) {
	};

	const char *what() const noexcept override {
		return m_whatString;
	}

private:
	const char *m_whatString = "";
};

#endif // EXCEPTION_HH
