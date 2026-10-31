#include "PVariableNode.h"
#include "Util/String.h"

namespace Animation::Procedural
{
	std::unique_ptr<PNodeInstanceData> PVariableNode::CreateInstanceData()
	{
		auto result = std::make_unique<InstanceData>();
		result->value = defaultValue;
		return result;
	}

	PEvaluationResult PVariableNode::Evaluate(PNodeInstanceData* a_instanceData, PoseCache&, PEvaluationContext&)
	{
		return static_cast<InstanceData*>(a_instanceData)->value;
	}

	void PVariableNode::Synchronize(PNodeInstanceData* a_instanceData, PNodeInstanceData* a_ownerInstance, float)
	{
		static_cast<InstanceData*>(a_instanceData)->value = static_cast<InstanceData*>(a_ownerInstance)->value;
	}

	bool PVariableNode::SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton*, const std::filesystem::path&)
	{
		if (a_values.size() >= 2) {
			name = Util::String::FromFixedString(std::get<RE::BSFixedString>(a_values[0]));
			defaultValue = std::get<float>(a_values[1]);
			name = Util::String::ToLower(name);
			syncId = std::hash<std::string>{}(name);
		}
		return true;
	}
}
