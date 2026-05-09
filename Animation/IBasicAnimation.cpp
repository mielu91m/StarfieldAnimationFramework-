#include "IBasicAnimation.h"
#include "Generator.h"

namespace Animation
{
	std::unique_ptr<Generator> IBasicAnimation::CreateGenerator()
	{
		return nullptr;
	}
}
