#include "PCH.h"
#include "GLTFImport.h"
#include "zstr.hpp"
#include "simdjson.h"
#include "Settings/Settings.h"
#include "Animation/Ozz.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/math.hpp>

#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/base/maths/math_ex.h>
#include <set>
#include <algorithm>
#include <variant>

namespace Serialization
{
	namespace
	{
		constexpr std::size_t kDebugChannelStart = 70;
		constexpr std::size_t kDebugChannelEnd = 90;

		bool IsAccessorReadable(const fastgltf::Asset& asset, const fastgltf::Accessor& accessor, const char* label, std::size_t channelIndex);

		struct AccessorView
		{
			const std::byte* base = nullptr;
			std::size_t stride = 0;
			std::size_t count = 0;
		};

		bool GetBufferBytes(const fastgltf::Asset& asset, std::size_t bufferIndex, const std::byte*& data, std::size_t& size)
		{
			if (bufferIndex >= asset.buffers.size()) {
				return false;
			}

			const auto& buffer = asset.buffers[bufferIndex];
			bool ok = std::visit([&](auto&& src) -> bool {
				using T = std::decay_t<decltype(src)>;
				if constexpr (std::is_same_v<T, fastgltf::sources::Array>) {
					data = src.bytes.data();
					size = src.bytes.size_bytes();
					return true;
				} else if constexpr (std::is_same_v<T, fastgltf::sources::Vector>) {
					data = src.bytes.data();
					size = src.bytes.size();
					return true;
				} else if constexpr (std::is_same_v<T, fastgltf::sources::ByteView>) {
					data = src.bytes.data();
					size = src.bytes.size();
					return true;
				} else {
					return false;
				}
			}, buffer.data);

			return ok && data && size > 0;
		}

		bool BuildAccessorView(const fastgltf::Asset& asset, const fastgltf::Accessor& accessor, AccessorView& out, const char* label, std::size_t channelIndex)
		{
			out = {};
			if (!IsAccessorReadable(asset, accessor, label, channelIndex)) {
				return false;
			}
			if (!accessor.bufferViewIndex.has_value()) {
				return false;
			}

			const auto& view = asset.bufferViews[*accessor.bufferViewIndex];
			const std::byte* data = nullptr;
			std::size_t dataSize = 0;
			if (!GetBufferBytes(asset, view.bufferIndex, data, dataSize)) {
				return false;
			}

			const auto elemSize = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
			const auto stride = view.byteStride.value_or(elemSize);
			const auto start = view.byteOffset + accessor.byteOffset;
			out.base = data + start;
			out.stride = stride;
			out.count = accessor.count;
			return out.base != nullptr && out.stride >= elemSize;
		}

		bool ReadAccessorFloats(const fastgltf::Asset& asset, const fastgltf::Accessor& accessor, std::size_t components, std::vector<float>& out, const char* label, std::size_t channelIndex)
		{
			out.clear();
			if (accessor.componentType != fastgltf::ComponentType::Float) {
				return false;
			}

			AccessorView view;
			if (!BuildAccessorView(asset, accessor, view, label, channelIndex)) {
				return false;
			}

			out.reserve(view.count * components);
			for (std::size_t i = 0; i < view.count; ++i) {
				const auto* ptr = view.base + i * view.stride;
				for (std::size_t c = 0; c < components; ++c) {
					float value = 0.0f;
					std::memcpy(&value, ptr + c * sizeof(float), sizeof(float));
					out.push_back(value);
				}
			}
			return true;
		}

		void LogAccessorDetails(const fastgltf::Asset& asset, const fastgltf::Accessor& accessor, const char* label, std::size_t channelIndex)
		{
			if (channelIndex < kDebugChannelStart || channelIndex > kDebugChannelEnd) {
				return;
			}

			const std::size_t viewIndex = accessor.bufferViewIndex ? *accessor.bufferViewIndex : static_cast<std::size_t>(-1);
			std::size_t bufferIndex = static_cast<std::size_t>(-1);
			std::size_t viewByteOffset = 0;
			std::size_t viewByteLength = 0;
			std::size_t viewStride = 0;

			if (viewIndex < asset.bufferViews.size()) {
				const auto& view = asset.bufferViews[viewIndex];
				bufferIndex = view.bufferIndex;
				viewByteOffset = view.byteOffset;
				viewByteLength = view.byteLength;
				viewStride = view.byteStride.value_or(0);
			}

			std::size_t bufferSize = 0;
			const std::byte* bufferData = nullptr;
			if (bufferIndex < asset.buffers.size()) {
				GetBufferBytes(asset, bufferIndex, bufferData, bufferSize);
			}

			SAF_LOG_INFO("CreateRawAnimation: channel {} {} accessor: count={}, type={}, comp={}, view={}, byteOffset={}, viewLen={}, viewOff={}, viewStride={}, buffer={}, bufferSize={}",
				channelIndex,
				label,
				accessor.count,
				static_cast<int>(accessor.type),
				static_cast<int>(accessor.componentType),
				viewIndex,
				accessor.byteOffset,
				viewByteLength,
				viewByteOffset,
				viewStride,
				bufferIndex,
				bufferSize);
		}

		bool IsAccessorReadable(const fastgltf::Asset& asset, const fastgltf::Accessor& accessor, const char* label, std::size_t channelIndex)
		{
			if (accessor.sparse && accessor.sparse->count > 0) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped ({} accessor uses sparse)", channelIndex, label);
				return false;
			}
			if (!accessor.bufferViewIndex.has_value()) {
				return true;
			}
			if (*accessor.bufferViewIndex >= asset.bufferViews.size()) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped ({} bufferView out of range)", channelIndex, label);
				return false;
			}

			const auto& view = asset.bufferViews[*accessor.bufferViewIndex];
			if (view.meshoptCompression) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped ({} bufferView uses meshopt compression)", channelIndex, label);
				return false;
			}

			const std::byte* data = nullptr;
			std::size_t dataSize = 0;
			if (!GetBufferBytes(asset, view.bufferIndex, data, dataSize)) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped ({} buffer data unavailable)", channelIndex, label);
				return false;
			}

			if (view.byteOffset + view.byteLength > dataSize) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped ({} bufferView out of buffer bounds)", channelIndex, label);
				return false;
			}

			const auto elemSize = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
			if (elemSize == 0 || accessor.count == 0) {
				return accessor.count == 0;
			}

			const auto stride = view.byteStride.value_or(elemSize);
			if (stride < elemSize) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped ({} stride < elemSize)", channelIndex, label);
				return false;
			}
			if (stride == 0) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped ({} stride=0)", channelIndex, label);
				return false;
			}

			const auto start = view.byteOffset + accessor.byteOffset;
			if (start > view.byteOffset + view.byteLength) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped ({} start out of view)", channelIndex, label);
				return false;
			}

			const auto lastIndex = accessor.count - 1;
			const auto maxExtra = (std::numeric_limits<std::size_t>::max() - start - elemSize) / stride;
			if (lastIndex > maxExtra) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped ({} bounds overflow)", channelIndex, label);
				return false;
			}

			const auto end = start + lastIndex * stride + elemSize;
			if (end > view.byteOffset + view.byteLength) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped ({} end out of view)", channelIndex, label);
				return false;
			}

			return true;
		}

		const char* ToPathName(fastgltf::AnimationPath path)
		{
			switch (path) {
			case fastgltf::AnimationPath::Translation:
				return "translation";
			case fastgltf::AnimationPath::Rotation:
				return "rotation";
			case fastgltf::AnimationPath::Scale:
				return "scale";
			case fastgltf::AnimationPath::Weights:
				return "weights";
			default:
				return "unknown";
			}
		}
	}

	void ParseMorphChannel(const GLTFImport::AssetData* assetData, const fastgltf::Animation* anim, const fastgltf::AnimationChannel& channel, Animation::RawOzzAnimation* rawAnim)
	{
		// TODO: Morph targets are optional for body animation; skip for stability.
		SAF_LOG_INFO("ParseMorphChannel: skipping morph channel for now");
		return;

		auto& asset = assetData->asset;
		if (!channel.nodeIndex.has_value()) return;
		if (channel.nodeIndex.value() >= asset.nodes.size()) return;
		auto& targetNode = asset.nodes[channel.nodeIndex.value()];

		if (!targetNode.meshIndex.has_value())
			return;

		auto iter = assetData->morphTargets.find(targetNode.meshIndex.value());
		if (iter == assetData->morphTargets.end())
			return;

		auto& morphTargets = iter->second;
		std::vector<size_t> morphIdxs;
		auto gameIdxs = Settings::GetFaceMorphIndexMap();
		bool hasMorphs = false;

		for (auto& mt : morphTargets) {
			if (auto mIter = gameIdxs.find(mt); mIter != gameIdxs.end()) {
				if (rawAnim->faceData != nullptr && !rawAnim->faceData->tracks[mIter->second].keyframes.empty()) {
					morphIdxs.push_back(UINT64_MAX);
				} else {
					morphIdxs.push_back(mIter->second);
					hasMorphs = true;
				}
			} else {
				morphIdxs.push_back(UINT64_MAX);
			}
		}

		if (!hasMorphs) return;

		auto& sampler = anim->samplers[channel.samplerIndex];
		auto& timeAccessor = asset.accessors[sampler.inputAccessor];
		auto& dataAccessor = asset.accessors[sampler.outputAccessor];
		if (!timeAccessor.bufferViewIndex.has_value() || !dataAccessor.bufferViewIndex.has_value()) return;
		if (timeAccessor.count == 0 || dataAccessor.count == 0) return;
		if (timeAccessor.componentType != fastgltf::ComponentType::Float || timeAccessor.type != fastgltf::AccessorType::Scalar) return;
		if (dataAccessor.componentType != fastgltf::ComponentType::Float) return;
		if (!IsAccessorReadable(asset, timeAccessor, "morph time", 0)) return;
		if (!IsAccessorReadable(asset, dataAccessor, "morph data", 0)) return;

		float duration = 0.0f;
		// Duration obliczymy z rzeczywistych wartości czasu
		
		if (rawAnim->faceData == nullptr) {
			rawAnim->faceData = std::make_unique<Animation::RawOzzFaceAnimation>();
		}

		ozz::animation::offline::RawTrackKeyframe<float> kf{};
		kf.interpolation = ozz::animation::offline::RawTrackInterpolation::kLinear;

		SAF_LOG_INFO("ParseMorphChannel: node={}, sampler={}, timeCount={}, dataCount={}",
			channel.nodeIndex.value(), channel.samplerIndex, timeAccessor.count, dataAccessor.count);

		std::vector<float> timeValues;
		if (!ReadAccessorFloats(asset, timeAccessor, 1, timeValues, "morph time", 0)) return;
		for (float t : timeValues) {
			if (t > duration) duration = t;
		}
		if (timeValues.empty()) return;
		
		rawAnim->faceData->duration = duration;

		std::vector<float> weightValues;
		if (!ReadAccessorFloats(asset, dataAccessor, 1, weightValues, "morph data", 0)) return;
		if (weightValues.empty()) return;
		if (morphTargets.empty()) return;
		if (morphTargets.size() > 0 && timeValues.size() > (std::numeric_limits<std::size_t>::max() / morphTargets.size())) {
			return;
		}
		const size_t required = timeValues.size() * morphTargets.size();
		if (weightValues.size() < required) return;

		for (size_t i = 0; i < timeAccessor.count; i++) {
			float time = timeValues[i];
			kf.ratio = (duration == 0.0f ? 0.0f : std::clamp(time / duration, 0.0f, 1.0f));

			const size_t baseIndex = i * morphTargets.size();
			if (baseIndex >= weightValues.size()) {
				break;
			}
			for (size_t j = 0; j < morphTargets.size(); j++) {
				auto idx = morphIdxs[j];
				if (idx == UINT64_MAX) continue;

				size_t weightIndex = baseIndex + j;
				if (weightIndex >= weightValues.size()) continue;
				kf.value = weightValues[weightIndex];
				rawAnim->faceData->tracks[idx].keyframes.push_back(kf);
			}
		}
	}

	std::unique_ptr<GLTFImport::SkeletonData> GLTFImport::BuildSkeleton(const AssetData* assetData)
	{
		auto result = std::make_unique<GLTFImport::SkeletonData>();
		std::set<std::string_view> nameSet;
		std::vector<std::string> nodeNames;

		for (size_t i = 0; i < assetData->asset.nodes.size(); i++) {
			const auto& n = assetData->asset.nodes[i];
			std::string_view curName = n.name;
			if (auto iter = assetData->originalNames.find(i); iter != assetData->originalNames.end()) {
				curName = iter->second;
			}

			if (!nameSet.contains(curName)) {
				nameSet.insert(curName);
				nodeNames.emplace_back(curName);
				auto& t = result->restPose.emplace_back();
				
				if (std::holds_alternative<fastgltf::TRS>(n.transform)) {
					auto& trs = std::get<fastgltf::TRS>(n.transform);
					t.rotation = { trs.rotation[0], trs.rotation[1], trs.rotation[2], trs.rotation[3] };
					t.translation = { trs.translation[0], trs.translation[1], trs.translation[2] };
				} else {
					t.rotation = { 0, 0, 0, 1 };
					t.translation = { 0, 0, 0 };
				}
				t.scale = ozz::math::Float3::one();
			}
		}

		ozz::animation::offline::RawSkeleton raw;
		for (auto& n : nodeNames) {
			auto& root = raw.roots.emplace_back();
			root.name = n;
			root.transform = ozz::math::Transform::identity();
		}

		ozz::animation::offline::SkeletonBuilder builder;
		result->skeleton = builder(raw);
		return result;
	}

std::unique_ptr<Animation::RawOzzAnimation> GLTFImport::CreateRawAnimation(
	const AssetData* assetData,
	const fastgltf::Animation* anim,
	const ozz::animation::Skeleton* skeleton,
	const std::vector<std::string>* jointNames)
	{
		auto& asset = assetData->asset;
		SAF_LOG_INFO("CreateRawAnimation: channels={}, nodes={}, samplers={}, accessors={}",
			anim->channels.size(), asset.nodes.size(), anim->samplers.size(), asset.accessors.size());
		std::vector<size_t> skeletonIdxs(asset.nodes.size(), UINT64_MAX);

	std::map<std::string, size_t> skeletonMap;
	size_t jointCount = 0;
	if (jointNames && !jointNames->empty()) {
		jointCount = jointNames->size();
		SAF_LOG_INFO("CreateRawAnimation: using jointNames (count={}, skeleton joints={})", jointNames->size(), skeleton ? skeleton->num_joints() : 0);
		for (size_t i = 0; i < jointNames->size(); i++) {
			const auto& name = (*jointNames)[i];
			if (!skeletonMap.contains(name)) {
				skeletonMap.emplace(name, i);
			}
		}
	} else if (skeleton) {
		jointCount = skeleton->num_joints();
		SAF_LOG_INFO("CreateRawAnimation: using skeleton->joint_names (count={})", jointCount);
		auto skeletonJointNames = skeleton->joint_names();
		for (size_t i = 0; i < skeletonJointNames.size(); i++) {
			const std::string name{ skeletonJointNames[i] };
			if (!skeletonMap.contains(name)) {
				skeletonMap.emplace(name, i);
			}
		}
	}

		ozz::math::Transform identity;
		identity.rotation = ozz::math::Quaternion::identity();
		identity.translation = ozz::math::Float3::zero();
		identity.scale = ozz::math::Float3::one();
	std::vector<ozz::math::Transform> bindPose(jointCount, identity);

		for (size_t i = 0; i < asset.nodes.size(); ++i) {
			const auto& n = asset.nodes[i];
			std::string nodeName = std::string(n.name);

			if (auto iter = assetData->originalNames.find(i); iter != assetData->originalNames.end()) {
				nodeName = iter->second;
			}

	if (auto iter = skeletonMap.find(nodeName); iter != skeletonMap.end()) {
		const size_t jointIndex = iter->second;
		skeletonIdxs[i] = jointIndex;
				if (std::holds_alternative<fastgltf::TRS>(n.transform)) {
					auto& trs = std::get<fastgltf::TRS>(n.transform);
			if (jointIndex < bindPose.size()) {
				auto& b = bindPose[jointIndex];
				b.rotation = { trs.rotation[0], trs.rotation[1], trs.rotation[2], trs.rotation[3] };
				b.translation = { trs.translation[0], trs.translation[1], trs.translation[2] };
				b.scale = { trs.scale[0], trs.scale[1], trs.scale[2] };
			}
				}
			}
		}

		auto rawAnimResult = std::make_unique<Animation::RawOzzAnimation>();
		rawAnimResult->data = ozz::make_unique<ozz::animation::offline::RawAnimation>();
		auto& animResult = rawAnimResult->data;
		animResult->duration = 0.0f;
	animResult->tracks.resize(jointCount);

		size_t channelIndex = 0;
		for (auto& c : anim->channels) {
			++channelIndex;
			SAF_LOG_INFO("CreateRawAnimation: channel {} begin path={} node={} sampler={}",
				channelIndex,
				ToPathName(c.path),
				c.nodeIndex.has_value() ? static_cast<int>(c.nodeIndex.value()) : -1,
				static_cast<int>(c.samplerIndex));
			if (!c.nodeIndex.has_value()) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (no nodeIndex)", channelIndex);
				continue;
			}
			if (c.nodeIndex.value() >= asset.nodes.size()) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (nodeIndex out of range: {})", channelIndex, c.nodeIndex.value());
				continue;
			}
			if (c.nodeIndex.value() >= skeletonIdxs.size()) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (skeletonIdxs out of range: {})", channelIndex, c.nodeIndex.value());
				continue;
			}
			if (c.samplerIndex >= anim->samplers.size()) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (samplerIndex out of range: {})", channelIndex, c.samplerIndex);
				continue;
			}

			if (c.path == fastgltf::AnimationPath::Weights) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} path=weights node={} sampler={}", channelIndex, c.nodeIndex.value(), c.samplerIndex);
				ParseMorphChannel(assetData, anim, c, rawAnimResult.get());
				continue;
			}

		auto idx = skeletonIdxs[c.nodeIndex.value()];
		if (idx == UINT64_MAX) continue;
		if (idx >= animResult->tracks.size()) {
			SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (joint index out of range: {})", channelIndex, idx);
			continue;
		}

			auto& sampler = anim->samplers[c.samplerIndex];
			if (sampler.inputAccessor >= asset.accessors.size() || sampler.outputAccessor >= asset.accessors.size()) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (accessor index out of range: in={}, out={})",
					channelIndex, sampler.inputAccessor, sampler.outputAccessor);
				continue;
			}
			auto& timeAcc = asset.accessors[sampler.inputAccessor];
			auto& dataAcc = asset.accessors[sampler.outputAccessor];
			if (!timeAcc.bufferViewIndex.has_value() || !dataAcc.bufferViewIndex.has_value()) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (missing bufferViewIndex)", channelIndex);
				continue;
			}
			LogAccessorDetails(asset, timeAcc, "time", channelIndex);
			LogAccessorDetails(asset, dataAcc, "data", channelIndex);
			if (!IsAccessorReadable(asset, timeAcc, "time", channelIndex) || !IsAccessorReadable(asset, dataAcc, "data", channelIndex)) {
				continue;
			}
			if (timeAcc.count == 0 || dataAcc.count == 0) continue;
			if (timeAcc.componentType != fastgltf::ComponentType::Float || timeAcc.type != fastgltf::AccessorType::Scalar) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (time accessor type mismatch)", channelIndex);
				continue;
			}
			if (dataAcc.componentType != fastgltf::ComponentType::Float) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (data accessor component type mismatch)", channelIndex);
				continue;
			}
			if (c.path == fastgltf::AnimationPath::Rotation && dataAcc.type != fastgltf::AccessorType::Vec4) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (rotation accessor not vec4)", channelIndex);
				continue;
			}
			if ((c.path == fastgltf::AnimationPath::Translation || c.path == fastgltf::AnimationPath::Scale) &&
				dataAcc.type != fastgltf::AccessorType::Vec3) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (vec3 expected for path {})", channelIndex, ToPathName(c.path));
				continue;
			}

			std::vector<float> times;
			if (!ReadAccessorFloats(asset, timeAcc, 1, times, "time", channelIndex)) {
				continue;
			}
			for (float t : times) {
				if (t > animResult->duration) animResult->duration = t;
			}
			if (times.empty()) continue;
			if (times.size() < dataAcc.count) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (times < data count: {} < {})", channelIndex, times.size(), dataAcc.count);
				continue;
			}
			if (dataAcc.count != timeAcc.count) {
				SAF_LOG_INFO("CreateRawAnimation: channel {} skipped (count mismatch: time={}, data={})", channelIndex, timeAcc.count, dataAcc.count);
				continue;
			}

			size_t i = 0;
			if (c.path == fastgltf::AnimationPath::Rotation) {
				std::vector<float> values;
				if (!ReadAccessorFloats(asset, dataAcc, 4, values, "data", channelIndex)) {
					continue;
				}
				const std::size_t frames = values.size() / 4;
				for (std::size_t f = 0; f < frames; ++f) {
					auto& k = animResult->tracks[idx].rotations.emplace_back();
					k.time = times[i++];
					k.value = { values[f * 4 + 0], values[f * 4 + 1], values[f * 4 + 2], values[f * 4 + 3] };
				}
			} else if (c.path == fastgltf::AnimationPath::Translation) {
				std::vector<float> values;
				if (!ReadAccessorFloats(asset, dataAcc, 3, values, "data", channelIndex)) {
					continue;
				}
				const std::size_t frames = values.size() / 3;
				for (std::size_t f = 0; f < frames; ++f) {
					auto& k = animResult->tracks[idx].translations.emplace_back();
					k.time = times[i++];
					k.value = { values[f * 3 + 0], values[f * 3 + 1], values[f * 3 + 2] };
				}
			} else if (c.path == fastgltf::AnimationPath::Scale) {
				std::vector<float> values;
				if (!ReadAccessorFloats(asset, dataAcc, 3, values, "data", channelIndex)) {
					continue;
				}
				const std::size_t frames = values.size() / 3;
				for (std::size_t f = 0; f < frames; ++f) {
					auto& k = animResult->tracks[idx].scales.emplace_back();
					k.time = times[i++];
					k.value = { values[f * 3 + 0], values[f * 3 + 1], values[f * 3 + 2] };
				}
			}
		}

	SAF_LOG_INFO("CreateRawAnimation: channel loop done, filling defaults");
		for (size_t i = 0; i < animResult->tracks.size(); ++i) {
			auto& t = animResult->tracks[i];
			if (t.rotations.empty()) t.rotations.push_back({0.0f, bindPose[i].rotation});
			if (t.translations.empty()) t.translations.push_back({0.0f, bindPose[i].translation});
			if (t.scales.empty()) t.scales.push_back({0.0f, bindPose[i].scale});
		}

	SAF_LOG_INFO("CreateRawAnimation: defaults filled");
		return rawAnimResult;
	}

	std::unique_ptr<GLTFImport::AssetData> GLTFImport::LoadGLTF(const std::filesystem::path& fileName)
	{
		try {
			fastgltf::Parser parser;
			auto dataResult = fastgltf::MappedGltfFile::FromPath(fileName);
			if (dataResult.error() != fastgltf::Error::None) return nullptr;

			auto mapped = std::make_shared<fastgltf::MappedGltfFile>(std::move(dataResult.get()));
			const auto ext = fileName.extension().string();
			const bool isBinary = (ext == ".glb" || ext == ".GLB");
			const auto options = fastgltf::Options::DecomposeNodeMatrices | fastgltf::Options::LoadExternalBuffers;

			if (isBinary) {
				auto gltf = parser.loadGltfBinary(*mapped, fileName.parent_path(), options);
				if (gltf.error() != fastgltf::Error::None) return nullptr;
				auto assetData = std::make_unique<AssetData>();
				assetData->asset = std::move(gltf.get());
				assetData->mappedFile = std::move(mapped);
				return assetData;
			} else {
				auto gltf = parser.loadGltf(*mapped, fileName.parent_path(), options);
				if (gltf.error() != fastgltf::Error::None) return nullptr;
				auto assetData = std::make_unique<AssetData>();
				assetData->asset = std::move(gltf.get());
				assetData->mappedFile = std::move(mapped);
				return assetData;
			}
		} catch (...) {
			return nullptr;
		}
	}
}