#pragma once

#include "PNode.h"

namespace Animation::Procedural
{
	class PVariableNode : public PNodeT<PVariableNode>
	{
	public:
		struct InstanceData : public PNodeInstanceDataT<InstanceData>
		{
			float value = 0.0f;
		};

		std::string name;
		float defaultValue = 0.0f;

		std::unique_ptr<PNodeInstanceData> CreateInstanceData() override;
		PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;
		void Synchronize(PNodeInstanceData* a_instanceData, PNodeInstanceData* a_ownerInstance, float a_correctionDelta) override;
		bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override;

		inline static Registration _reg{
			"var", {}, { { "name", PEvaluationType<RE::BSFixedString> }, { "defVal", PEvaluationType<float> } },
			PEvaluationType<float>, CreateNodeOfType<PVariableNode>, "", kValueCreators, ""
		};
	};

	struct PVariableInstance : public PVariableNode::InstanceData {};
}
