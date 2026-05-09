#include "SpringBoneJob.h"

namespace Animation
{
	bool SpringBoneJob::Run(const Context&)
	{
		return true;
	}

	IPostGenJob::GUID SpringBoneJob::GetGUID()
	{
		GUID result;
		result.parts.jobType = 0x53505247u; // 'SPRG'
		result.parts.instanceNum = springId;
		return result;
	}

	void SpringBoneJob::Destroy() { delete this; }
}
