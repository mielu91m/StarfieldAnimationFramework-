#include "Animation/Procedural/PStaticPoseNode.h"
#include "Animation/FileManager.h"
#include "Animation/IBasicAnimation.h"

namespace Animation::Procedural
{
	PEvaluationResult PStaticPoseNode::Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext)
	{
		PoseCache::Handle result = a_poseCache.acquire_handle();
		auto resultSpan = result.get();
		std::copy(pose.begin(), pose.end(), resultSpan.begin());

		return PEvaluationResult(std::in_place_type<PoseCache::Handle>, std::move(result));
	}

	bool PStaticPoseNode::SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir)
	{
		std::filesystem::path fullPath = a_localDir;
		fullPath /= std::get<RE::BSFixedString>(a_values[0]).c_str();
		auto file = FileID{ fullPath.generic_string(), "" };
		auto loadedFile = Animation::FileManager::GetSingleton()->DemandAnimation(file, a_skeleton->name, true);
		if (loadedFile == nullptr) {
			return false;
		}

		auto anim = std::dynamic_pointer_cast<IBasicAnimation>(loadedFile);
		if (anim == nullptr || !anim->HasBoneAnimation()) {
			return false;
		}

		const int numJoints = a_skeleton->data->num_joints();
		const size_t soaCount = static_cast<size_t>((numJoints + 3) / 4);
		pose.resize(soaCount);

		std::unique_ptr<IBasicAnimationContext> ctx = anim->CreateContext();
		ozz::span<ozz::math::SoaTransform> outSpan = ozz::make_span(pose);
		anim->SampleBoneAnimation(0.0f, outSpan, ctx.get());
		return true;
	}

	size_t PStaticPoseNode::GetSizeBytes()
	{
		return sizeof(PStaticPoseNode) + std::span(pose).size_bytes();
	}
}