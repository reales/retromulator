#include "logging.h"

#include <fstream>
#include <vector>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ctime>
#define output_string(s) ::OutputDebugStringA(s)
#else
#define output_string(s) fputs(s, stderr);
#endif

namespace mc68k
{
	namespace
	{
		LogFunc g_logFunc = nullptr;
	}

	void setLogFunc(LogFunc _func)
	{
		g_logFunc = _func;
	}

	void logToConsole( const std::string& _s )
	{
		if(g_logFunc) { g_logFunc(_s); return; }
		output_string( (_s + "\n").c_str() );
	}
}

