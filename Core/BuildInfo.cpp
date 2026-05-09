#include "Core/BuildInfo.h"

namespace SAF::Core
{
	const char* GetBuildStamp()
	{
		return __DATE__ " " __TIME__;
	}
}
