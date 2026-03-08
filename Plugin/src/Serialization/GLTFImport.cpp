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
#include <fstream>
#include <zlib.h>

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

		const auto& boneAliases = Settings::GetGLTFBoneAliases();

		for (size_t i = 0; i < asset.nodes.size(); ++i) {
			const auto& n = asset.nodes[i];
			std::string nodeName = std::string(n.name);

			if (auto iter = assetData->originalNames.find(i); iter != assetData->originalNames.end()) {
				nodeName = iter->second;
			}

			std::string lookupName = nodeName;
			auto iter = skeletonMap.find(lookupName);
			if (iter == skeletonMap.end() && !boneAliases.empty()) {
				auto ait = boneAliases.find(nodeName);
				if (ait != boneAliases.end()) lookupName = ait->second;
				iter = skeletonMap.find(lookupName);
			}
			if (iter != skeletonMap.end()) {
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
		// Track which joints are actually animated by the clip (any TRS channel present).
		// This lets callers skip applying defaults to joints that aren't animated (especially face bones).
		rawAnimResult->jointHasChannel.assign(jointCount, static_cast<std::uint8_t>(0));

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
				rawAnimResult->jointHasChannel[idx] = static_cast<std::uint8_t>(1);
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
				rawAnimResult->jointHasChannel[idx] = static_cast<std::uint8_t>(1);
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
				rawAnimResult->jointHasChannel[idx] = static_cast<std::uint8_t>(1);
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

	namespace
	{
		std::string s_lastGLTFLoadError;

		// Naprawia JSON glTF: zamienia "wersja" na "version" (eksport NAF/Snapdragon). Zwraca poprawioną treść lub pustą przy błędzie.
		static bool FixWersjaInJson(std::string& content)
		{
			const std::string wrongKey = "\"wersja\":";
			const std::string rightKey = "\"version\":";
			for (size_t pos = 0; (pos = content.find(wrongKey, pos)) != std::string::npos; pos += rightKey.size())
				content.replace(pos, wrongKey.size(), rightKey);
			return true;
		}

		// Ładuje glTF z bufora JSON (po ewentualnej naprawie wersja). content musi zaczynać się od '{' (po BOM/ws).
		static std::unique_ptr<GLTFImport::AssetData> LoadJsonGltfAndGetAsset(std::string& content, const std::filesystem::path& directory, fastgltf::Parser& parser)
		{
			FixWersjaInJson(content);
			std::vector<std::byte> bytes(content.size());
			std::memcpy(bytes.data(), content.data(), content.size());
			auto bufResult = fastgltf::GltfDataBuffer::FromBytes(bytes.data(), bytes.size());
			if (bufResult.error() != fastgltf::Error::None) return nullptr;
			auto gltf = parser.loadGltfJson(bufResult.get(), directory, fastgltf::Options::DecomposeNodeMatrices | fastgltf::Options::LoadExternalBuffers);
			if (gltf.error() != fastgltf::Error::None) return nullptr;
			auto assetData = std::make_unique<GLTFImport::AssetData>();
			assetData->asset = std::move(gltf.get());
			return assetData;
		}

		// Fallback: plik to gzip (np. "GLB" = gzip-compressed JSON jak u Ebanex). 1F 8B = gzip magic.
		static std::unique_ptr<GLTFImport::AssetData> TryLoadGLTFFromGzip(const std::filesystem::path& fileName, fastgltf::Parser& parser)
		{
			std::ifstream f(fileName, std::ios::binary);
			if (!f) return nullptr;
			std::string gzipContent((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			f.close();
			if (gzipContent.size() < 18u) return nullptr;
			const unsigned char* p = reinterpret_cast<const unsigned char*>(gzipContent.data());
			if (p[0] != 0x1F || p[1] != 0x8B) {
				SAF_LOG_INFO("LoadGLTF fallback GZIP: '{}' - not gzip (first bytes {:02X} {:02X})", fileName.string(), p[0], p[1]);
				return nullptr;
			}
			// Decompress whole gzip stream (zlib with windowBits 15+16 = gzip)
			std::vector<uint8_t> uncompressed;
			uncompressed.resize(gzipContent.size() * 8u); // initial guess
			z_stream strm = {};
			strm.next_in = const_cast<unsigned char*>(p);
			strm.avail_in = static_cast<unsigned>(gzipContent.size());
			strm.next_out = uncompressed.data();
			strm.avail_out = static_cast<unsigned>(uncompressed.size());
			if (inflateInit2(&strm, 15 + 16) != Z_OK) { // 15+16 = gzip
				SAF_LOG_WARN("LoadGLTF fallback GZIP: '{}' - inflateInit2 failed", fileName.string());
				return nullptr;
			}
			int ret = inflate(&strm, Z_FINISH);
			inflateEnd(&strm);
			if (ret != Z_STREAM_END && ret != Z_OK) {
				if (ret == Z_BUF_ERROR) {
					// Output buffer too small, retry with larger
					uncompressed.resize(gzipContent.size() * 32u);
					strm = {};
					strm.next_in = const_cast<unsigned char*>(p);
					strm.avail_in = static_cast<unsigned>(gzipContent.size());
					strm.next_out = uncompressed.data();
					strm.avail_out = static_cast<unsigned>(uncompressed.size());
					if (inflateInit2(&strm, 15 + 16) != Z_OK) return nullptr;
					ret = inflate(&strm, Z_FINISH);
					inflateEnd(&strm);
				}
				if (ret != Z_STREAM_END && ret != Z_OK) {
					SAF_LOG_WARN("LoadGLTF fallback GZIP: '{}' - inflate failed (ret={})", fileName.string(), ret);
					return nullptr;
				}
			}
			size_t outLen = strm.total_out;
			const unsigned char* dec = uncompressed.data();
			size_t start = 0;
			while (start < outLen && (dec[start] <= 32 || dec[start] == 0xEF || dec[start] == 0xBB || dec[start] == 0xBF))
				++start;
			if (start >= outLen) return nullptr;
			// Binary GLB magic "glTF" (0x67 0x6C 0x54 0x46) – Ebanex often ships gzip-compressed GLB
			if (start + 4 <= outLen && dec[start] == 0x67 && dec[start + 1] == 0x6C && dec[start + 2] == 0x54 && dec[start + 3] == 0x46) {
				auto owned = std::make_shared<std::vector<std::byte>>(
					reinterpret_cast<const std::byte*>(dec + start), reinterpret_cast<const std::byte*>(dec + outLen));
				auto bufResult = fastgltf::GltfDataBuffer::FromBytes(owned->data(), owned->size());
				if (bufResult.error() != fastgltf::Error::None) {
					SAF_LOG_WARN("LoadGLTF fallback GZIP: '{}' - GltfDataBuffer::FromBytes failed", fileName.string());
					return nullptr;
				}
				const auto options = fastgltf::Options::DecomposeNodeMatrices | fastgltf::Options::LoadExternalBuffers | fastgltf::Options::DontRequireValidAssetMember;
				const auto categories = fastgltf::Category::Animations | fastgltf::Category::Nodes | fastgltf::Category::Meshes | fastgltf::Category::Buffers | fastgltf::Category::BufferViews | fastgltf::Category::Samplers | fastgltf::Category::Accessors | fastgltf::Category::Asset;
				auto gltf = parser.loadGltfBinary(bufResult.get(), fileName.parent_path(), options, categories);
				if (gltf.error() != fastgltf::Error::None) {
					const char* section = fastgltf::getLastParseErrorSection();
					SAF_LOG_WARN("LoadGLTF fallback GZIP: '{}' - loadGltfBinary failed: {} (parse section: {})", fileName.string(), fastgltf::getErrorMessage(gltf.error()).data(), section && *section ? section : "?");
					return nullptr;
				}
				auto assetData = std::make_unique<GLTFImport::AssetData>();
				assetData->asset = std::move(gltf.get());
				assetData->optionalBuffer = std::move(owned);
				return assetData;
			}
			if (dec[start] == '{') {
				std::string content(reinterpret_cast<const char*>(dec + start), outLen - start);
				return LoadJsonGltfAndGetAsset(content, fileName.parent_path(), parser);
			}
			SAF_LOG_INFO("LoadGLTF fallback GZIP: '{}' - decompressed content is not JSON or GLB (starts with 0x{:02X})", fileName.string(), dec[start]);
			return nullptr;
		}

		// Fallback: plik to surowy JSON (np. .glb = JSON). Naprawa "wersja" -> "version", loadGltfJson.
		static std::unique_ptr<GLTFImport::AssetData> TryLoadGLTFAsRawJson(const std::filesystem::path& fileName, fastgltf::Parser& parser)
		{
			std::ifstream f(fileName, std::ios::binary);
			if (!f) return nullptr;
			std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			f.close();
			if (content.empty()) return nullptr;
			size_t start = 0;
			while (start < content.size() && (static_cast<unsigned char>(content[start]) == 0xEF || static_cast<unsigned char>(content[start]) == 0xBB || static_cast<unsigned char>(content[start]) == 0xBF || content[start] <= 32))
				++start;
			if (start >= content.size() || content[start] != '{') return nullptr;
			content = content.substr(start);
			return LoadJsonGltfAndGetAsset(content, fileName.parent_path(), parser);
		}

		// Fallback: plik to ZIP (np. "GLB" = archiwum ZIP z JSON w środku, jak w NAF). Rozpakowuje pierwszy plik (deflate), naprawa "wersja", loadGltfJson.
		static std::unique_ptr<GLTFImport::AssetData> TryLoadGLTFFromZip(const std::filesystem::path& fileName, fastgltf::Parser& parser)
		{
			std::ifstream f(fileName, std::ios::binary);
			if (!f) return nullptr;
			std::string zipContent((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			f.close();
			if (zipContent.size() < 30u) return nullptr;
			const unsigned char* p = reinterpret_cast<const unsigned char*>(zipContent.data());
			// Standard ZIP local file header: PK\x03\x04
			if (p[0] != 0x50 || p[1] != 0x4B || p[2] != 0x03 || p[3] != 0x04) {
				SAF_LOG_INFO("LoadGLTF fallback ZIP: '{}' - not a ZIP (first bytes {:02X} {:02X} {:02X} {:02X})", fileName.string(), p[0], p[1], p[2], p[3]);
				return nullptr;
			}
			uint16_t method = p[8] | (p[9] << 8);
			uint32_t compSize = p[18] | (p[19] << 8) | (p[20] << 16) | (p[21] << 24);
			uint32_t uncompSize = p[22] | (p[23] << 8) | (p[24] << 16) | (p[25] << 24);
			uint16_t nameLen = p[26] | (p[27] << 8);
			uint16_t extraLen = p[28] | (p[29] << 8);
			size_t dataOff = 30 + nameLen + extraLen;
			if (dataOff + compSize > zipContent.size()) {
				SAF_LOG_INFO("LoadGLTF fallback ZIP: '{}' - size overflow", fileName.string());
				return nullptr;
			}
			std::string content;
			if (method == 0) {
				// Stored (no compression)
				content.assign(zipContent.begin() + dataOff, zipContent.begin() + dataOff + compSize);
			} else if (method == 8) {
				// Deflate
				std::vector<uint8_t> uncompressed;
				if (uncompSize > 0u && uncompSize < 50 * 1024 * 1024u) uncompressed.resize(uncompSize);
				else uncompressed.resize(compSize * 4u);
				z_stream strm = {};
				strm.next_in = const_cast<unsigned char*>(p + dataOff);
				strm.avail_in = static_cast<unsigned>(compSize);
				strm.next_out = uncompressed.data();
				strm.avail_out = static_cast<unsigned>(uncompressed.size());
				if (inflateInit2(&strm, -15) != Z_OK) {
					SAF_LOG_WARN("LoadGLTF fallback ZIP: '{}' - inflateInit2 failed", fileName.string());
					return nullptr;
				}
				int ret = inflate(&strm, Z_FINISH);
				inflateEnd(&strm);
				if (ret != Z_STREAM_END && ret != Z_OK) {
					SAF_LOG_WARN("LoadGLTF fallback ZIP: '{}' - inflate failed (ret={})", fileName.string(), ret);
					return nullptr;
				}
				size_t outLen = strm.total_out;
				content.assign(uncompressed.begin(), uncompressed.begin() + outLen);
			} else {
				SAF_LOG_INFO("LoadGLTF fallback ZIP: '{}' - unsupported compression method={} (0=stored, 8=deflate)", fileName.string(), static_cast<int>(method));
				return nullptr;
			}
			size_t start = 0;
			while (start < content.size() && (static_cast<unsigned char>(content[start]) <= 32 || content[start] == '\xEF' || content[start] == '\xBB' || content[start] == '\xBF'))
				++start;
			if (start >= content.size() || content[start] != '{') {
				SAF_LOG_INFO("LoadGLTF fallback ZIP: '{}' - first entry is not JSON (starts with 0x{:02X})", fileName.string(), start < content.size() ? static_cast<unsigned char>(content[start]) : 0u);
				return nullptr;
			}
			content = content.substr(start);
			return LoadJsonGltfAndGetAsset(content, fileName.parent_path(), parser);
		}
	}

	const std::string& GLTFImport::GetLastGLTFLoadError()
	{
		return s_lastGLTFLoadError;
	}

	std::unique_ptr<GLTFImport::AssetData> GLTFImport::LoadGLTF(const std::filesystem::path& fileName)
	{
		s_lastGLTFLoadError.clear();
		try {
			// NAF-style: read through zstr (gzip decompression) then loadGltf – same as NativeAnimationFrameworkSF
			{
				const std::string pathStr = fileName.generic_string();
				zstr::ifstream file(pathStr, std::ios::binary);
				if (file.bad()) {
					SAF_LOG_INFO("LoadGLTF: NAF-style zstr open failed for '{}'", fileName.string());
				} else {
					std::vector<std::byte> buffer;
					for (std::istreambuf_iterator<char> it(file), end; it != end; ++it)
						buffer.push_back(static_cast<std::byte>(*it));
					if (buffer.empty()) {
						SAF_LOG_INFO("LoadGLTF: NAF-style read empty for '{}'", fileName.string());
					} else {
						auto bufResult = fastgltf::GltfDataBuffer::FromBytes(buffer.data(), buffer.size());
						if (bufResult.error() != fastgltf::Error::None) {
							SAF_LOG_INFO("LoadGLTF: NAF-style FromBytes failed for '{}'", fileName.string());
						} else {
							fastgltf::Parser parser;
							const auto options = fastgltf::Options::DecomposeNodeMatrices | fastgltf::Options::LoadExternalBuffers | fastgltf::Options::DontRequireValidAssetMember;
							const auto categories = fastgltf::Category::Animations | fastgltf::Category::Nodes | fastgltf::Category::Meshes | fastgltf::Category::Buffers | fastgltf::Category::BufferViews | fastgltf::Category::Samplers | fastgltf::Category::Accessors | fastgltf::Category::Asset;
							auto gltf = parser.loadGltf(bufResult.get(), fileName.parent_path(), options, categories);
							if (gltf.error() == fastgltf::Error::None) {
								auto assetData = std::make_unique<AssetData>();
								assetData->asset = std::move(gltf.get());
								SAF_LOG_INFO("LoadGLTF: NAF-style load succeeded for '{}'", fileName.string());
								return assetData;
							}
							const char* section = fastgltf::getLastParseErrorSection();
							SAF_LOG_INFO("LoadGLTF: NAF-style loadGltf failed for '{}': {} (parse section: {})", fileName.string(), fastgltf::getErrorMessage(gltf.error()).data(), section && *section ? section : "?");
						}
					}
				}
			}

			fastgltf::Parser parser;
			auto dataResult = fastgltf::MappedGltfFile::FromPath(fileName);
			if (dataResult.error() != fastgltf::Error::None) {
				s_lastGLTFLoadError = std::string("File: ") + fastgltf::getErrorMessage(dataResult.error()).data();
				{ auto fallback = TryLoadGLTFFromGzip(fileName, parser); if (fallback) return fallback; }
				{ auto fallback = TryLoadGLTFFromZip(fileName, parser); if (fallback) return fallback; }
				{ auto fallback = TryLoadGLTFAsRawJson(fileName, parser); if (fallback) return fallback; }
				return nullptr;
			}

			auto mapped = std::make_shared<fastgltf::MappedGltfFile>(std::move(dataResult.get()));
			const auto ext = fileName.extension().string();
			const bool isBinary = (ext == ".glb" || ext == ".GLB");
			const auto options = fastgltf::Options::DecomposeNodeMatrices | fastgltf::Options::LoadExternalBuffers;

			if (isBinary) {
				auto gltf = parser.loadGltfBinary(*mapped, fileName.parent_path(), options);
				if (gltf.error() != fastgltf::Error::None) {
					s_lastGLTFLoadError = std::string("GLB parse: ") + fastgltf::getErrorMessage(gltf.error()).data();
					SAF_LOG_INFO("LoadGLTF: GLB parse failed for '{}', trying fallback GZIP, ZIP, then raw JSON", fileName.string());
					{ auto fallback = TryLoadGLTFFromGzip(fileName, parser); if (fallback) { SAF_LOG_INFO("LoadGLTF: fallback GZIP succeeded for '{}'", fileName.string()); return fallback; } SAF_LOG_WARN("LoadGLTF: fallback GZIP failed for '{}'", fileName.string()); }
					{ auto fallback = TryLoadGLTFFromZip(fileName, parser); if (fallback) { SAF_LOG_INFO("LoadGLTF: fallback ZIP succeeded for '{}'", fileName.string()); return fallback; } SAF_LOG_WARN("LoadGLTF: fallback ZIP failed for '{}'", fileName.string()); }
					{ auto fallback = TryLoadGLTFAsRawJson(fileName, parser); if (fallback) { SAF_LOG_INFO("LoadGLTF: fallback raw JSON succeeded for '{}'", fileName.string()); return fallback; } SAF_LOG_WARN("LoadGLTF: fallback raw JSON failed for '{}'", fileName.string()); }
					return nullptr;
				}
				auto assetData = std::make_unique<AssetData>();
				assetData->asset = std::move(gltf.get());
				assetData->mappedFile = std::move(mapped);
				return assetData;
			} else {
				auto gltf = parser.loadGltf(*mapped, fileName.parent_path(), options);
				if (gltf.error() != fastgltf::Error::None) {
					s_lastGLTFLoadError = std::string("GLTF parse: ") + fastgltf::getErrorMessage(gltf.error()).data();
					{ auto fallback = TryLoadGLTFFromGzip(fileName, parser); if (fallback) return fallback; }
					{ auto fallback = TryLoadGLTFFromZip(fileName, parser); if (fallback) return fallback; }
					{ auto fallback = TryLoadGLTFAsRawJson(fileName, parser); if (fallback) return fallback; }
					return nullptr;
				}
				auto assetData = std::make_unique<AssetData>();
				assetData->asset = std::move(gltf.get());
				assetData->mappedFile = std::move(mapped);
				return assetData;
			}
		} catch (const std::exception& e) {
			s_lastGLTFLoadError = std::string("Exception: ") + e.what();
			return nullptr;
		} catch (...) {
			s_lastGLTFLoadError = "Unknown exception.";
			return nullptr;
		}
	}
}