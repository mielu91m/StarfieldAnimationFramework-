#pragma once
#include "Animation/Ozz.h"
#include <map>
#include <vector>
#include <filesystem>
#include <memory>
#include "fastgltf/core.hpp"
#include "fastgltf/types.hpp"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/transform.h"

namespace Serialization
{
	class GLTFImport
	{
	public:
		struct AssetData
		{
			fastgltf::Asset asset;
			std::map<size_t, std::vector<std::string>> morphTargets;
			std::map<size_t, std::string> originalNames;
			std::shared_ptr<fastgltf::MappedGltfFile> mappedFile;
			/// When loading gzip-compressed binary GLB, buffer data is held here (mappedFile is null).
			std::shared_ptr<std::vector<std::byte>> optionalBuffer;
		};

		struct SkeletonData
		{
			ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
			std::vector<ozz::math::Transform> restPose;
		};
		
		static std::unique_ptr<SkeletonData> BuildSkeleton(const AssetData* assetData);
		static std::unique_ptr<Animation::RawOzzAnimation> CreateRawAnimation(
			const AssetData* assetData,
			const fastgltf::Animation* anim,
			const ozz::animation::Skeleton* skeleton,
			const std::vector<std::string>* jointNames = nullptr);
		static std::unique_ptr<AssetData> LoadGLTF(const std::filesystem::path& fileName);
		/// Last error from LoadGLTF when it returned nullptr (file/parse error or exception).
		static const std::string& GetLastGLTFLoadError();
	};
}