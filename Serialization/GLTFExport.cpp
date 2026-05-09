#include "GLTFExport.h"
#include "Settings/Settings.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/math_ex.h>
#include <ozz/animation/offline/animation_optimizer.h>
#include <ozz/animation/offline/raw_animation.h>
#include <memory>
#include <map>
#include <set>
#include <optional>
#include <functional>
#include <variant>

#undef min
#undef max

namespace Serialization
{
    namespace
    {
        constexpr size_t Vec4Size = sizeof(float) * 4;
        constexpr size_t Vec3Size = sizeof(float) * 3;
        constexpr size_t ScalarSize = sizeof(float);

        enum BufferType
        {
            Rot,
            Time,
            Trans,
            Scale,
            Morphs
        };

        struct ExportUtil
        {
            void Init(const ozz::animation::Skeleton* skeleton)
            {
                asset = std::make_unique<fastgltf::Asset>();
                auto joints = skeleton->joint_names();
                for (auto& j : joints) {
                    fastgltf::Node n;
                    n.name = j;
                    
                    fastgltf::TRS trs;
                    trs.translation = fastgltf::math::fvec3(0.0f, 0.0f, 0.0f);
                    trs.rotation = fastgltf::math::fquat(0.0f, 0.0f, 0.0f, 1.0f);
                    trs.scale = fastgltf::math::fvec3(1.0f, 1.0f, 1.0f);
                    n.transform = trs;

                    asset->nodes.push_back(n);
                }
            }

            size_t MakeBView(size_t bufferIdx, size_t length)
            {
                auto& bv = asset->bufferViews.emplace_back();
                bv.bufferIndex = bufferIdx;
                bv.byteLength = length;
                bv.byteOffset = 0;
                return (asset->bufferViews.size() - 1);
            }

            fastgltf::sources::Vector& MakeBuffer(size_t length)
            {
                auto& buf = asset->buffers.emplace_back();
                buf.data.emplace<fastgltf::sources::Vector>();
                auto& bVec = std::get<fastgltf::sources::Vector>(buf.data);
                bVec.bytes.resize(length);
                buf.byteLength = length;
                return bVec;
            }

            size_t WriteBuffer(size_t unitSize, size_t length, BufferType bufType, const std::function<void*(size_t)>& getFunc)
            {
                auto& buf = MakeBuffer(unitSize * length);
                for (size_t i = 0; i < length; i++) {
                    memcpy(&buf.bytes[i * unitSize], getFunc(i), unitSize);
                }
                return DedupeLastBuffer(bufType, buf);
            }

            size_t MakeAccessor(double minVal, double maxVal, fastgltf::AccessorType type, size_t count, size_t bufferView)
            {
                auto& acc = asset->accessors.emplace_back();
                
                // Pomijamy min/max - są opcjonalne w specyfikacji GLTF
                // i fastgltf nie udostępnia prostego API do ich ustawienia
                
                acc.type = type;
                acc.count = count;
                acc.componentType = fastgltf::ComponentType::Float;
                acc.bufferViewIndex = bufferView;

                return (asset->accessors.size() - 1);
            }

            size_t WriteAccessor(double min, double max, fastgltf::AccessorType type, size_t length, BufferType bufType, const std::function<void*(size_t)>& getFunc)
            {
                size_t unitSize = 0;
                switch (type) {
                case fastgltf::AccessorType::Scalar: unitSize = ScalarSize; break;
                case fastgltf::AccessorType::Vec3:   unitSize = Vec3Size; break;
                case fastgltf::AccessorType::Vec4:   unitSize = Vec4Size; break;
                default: break;
                }

                auto bufIdx = WriteBuffer(unitSize, length, bufType, getFunc);
                bufferMap[bufType].insert(bufIdx);

                if (auto iter = bufferAccMap.find(bufIdx); iter != bufferAccMap.end()) {
                    return iter->second;
                }

                auto bvIdx = MakeBView(bufIdx, unitSize * length);
                auto accIdx = MakeAccessor(min, max, type, length, bvIdx);
                bufferAccMap[bufIdx] = accIdx;
                return accIdx;
            }

            size_t DedupeLastBuffer(BufferType bufType, fastgltf::sources::Vector& cmp)
            {
                auto iter = bufferMap.find(bufType);
                if (iter == bufferMap.end()) return (asset->buffers.size() - 1);

                for (auto& idx : iter->second) {
                    auto& vec = std::get<fastgltf::sources::Vector>(asset->buffers[idx].data);
                    if (vec.bytes.size() != cmp.bytes.size()) continue;

                    if (memcmp(vec.bytes.data(), cmp.bytes.data(), cmp.bytes.size()) == 0) {
                        asset->buffers.pop_back();
                        return idx;
                    }
                }
                return (asset->buffers.size() - 1);
            }

            void CombineBuffers()
            {
                if (asset->buffers.empty()) return;

                fastgltf::Buffer combinedBuf;
                combinedBuf.data.emplace<fastgltf::sources::Vector>();
                auto& combinedVec = std::get<fastgltf::sources::Vector>(combinedBuf.data);

                std::vector<size_t> offsets;
                offsets.reserve(asset->buffers.size());

                for (auto& b : asset->buffers) {
                    offsets.push_back(combinedVec.bytes.size());
                    auto& curVec = std::get<fastgltf::sources::Vector>(b.data);
                    combinedVec.bytes.insert(combinedVec.bytes.end(), curVec.bytes.begin(), curVec.bytes.end());
                }

                combinedBuf.byteLength = combinedVec.bytes.size();
                
                for (size_t i = 0; i < asset->bufferViews.size(); ++i) {
                    auto& bv = asset->bufferViews[i];
                    if (bv.bufferIndex < offsets.size()) {
                        bv.byteOffset = offsets[bv.bufferIndex];
                        bv.bufferIndex = 0;
                    }
                }

                asset->buffers.clear();
                asset->buffers.push_back(std::move(combinedBuf));
            }

            std::map<size_t, size_t> bufferAccMap;
            std::map<BufferType, std::set<size_t>> bufferMap;
            std::unique_ptr<fastgltf::Asset> asset;
        };
    }

    std::vector<std::byte> GLTFExport::CreateOptimizedAsset(Animation::RawOzzAnimation* anim, const ozz::animation::Skeleton* skeleton, uint8_t level)
    {
        if (level > 0) {
            float toleranceLevel = 1e-5f;
            switch (level) {
                case 1: toleranceLevel = 0.0f; break;
                case 2: toleranceLevel = 1e-7f; break;
                case 3: toleranceLevel = 1e-6f; break;
            }

            auto tempAnim = ozz::make_unique<ozz::animation::offline::RawAnimation>();
            ozz::animation::offline::AnimationOptimizer optimizer;
            optimizer.setting.tolerance = toleranceLevel;
            optimizer(*anim->data, *skeleton, tempAnim.get());
            anim->data = std::move(tempAnim);
        }

        ExportUtil util;
        util.Init(skeleton);
        auto& asset = util.asset;

        auto& assetAnim = asset->animations.emplace_back();
        assetAnim.name = "Animation";

        const auto DedupeSampler = [&](const fastgltf::AnimationSampler& smplr) -> size_t {
            if (assetAnim.samplers.size() <= 1) return (assetAnim.samplers.size() - 1);
            for (size_t i = 0; i < (assetAnim.samplers.size() - 1); i++) {
                auto& s = assetAnim.samplers[i];
                if (s.inputAccessor == smplr.inputAccessor && s.outputAccessor == smplr.outputAccessor) {
                    assetAnim.samplers.pop_back();
                    return i;
                }
            }
            return (assetAnim.samplers.size() - 1);
        };

        for (size_t i = 0; i < anim->data->tracks.size(); i++) {
            auto& trck = anim->data->tracks[i];
            if (trck.rotations.empty()) continue;

            auto& rotSmplr = assetAnim.samplers.emplace_back();
            rotSmplr.interpolation = fastgltf::AnimationInterpolation::Linear;
            rotSmplr.inputAccessor = util.WriteAccessor(trck.rotations.front().time, trck.rotations.back().time, fastgltf::AccessorType::Scalar, trck.rotations.size(), BufferType::Time, [&](size_t idx) { return &trck.rotations[idx].time; });
            rotSmplr.outputAccessor = util.WriteAccessor(0.0, 0.0, fastgltf::AccessorType::Vec4, trck.rotations.size(), BufferType::Rot, [&](size_t idx) { return &trck.rotations[idx].value; });

            auto& rotChnl = assetAnim.channels.emplace_back();
            rotChnl.nodeIndex = i;
            rotChnl.path = fastgltf::AnimationPath::Rotation;
            rotChnl.samplerIndex = DedupeSampler(rotSmplr);
        }

        // MorphTargets
        std::vector<std::string> morphNames = Settings::GetFaceMorphs();
        if (anim->faceData != nullptr && !morphNames.empty()) {
            auto& n = asset->nodes.emplace_back();
            n.name = "_MorphTarget_";
            n.meshIndex = 0;

            auto& mesh = asset->meshes.emplace_back();
            mesh.name = "_MorphTarget_";
            auto& mPrim = mesh.primitives.emplace_back();
            
            // Poprawka: emplace_back dla SmallVector
            mPrim.attributes.emplace_back("POSITION", 0);
        }

        util.CombineBuffers();
        fastgltf::Exporter exp;
        
        auto result = exp.writeGltfBinary(*asset);
        if (result.error() != fastgltf::Error::None) return {};
        
        auto& glb = result.get();
        std::vector<std::byte> out;
        out.resize(glb.output.size());
        std::memcpy(out.data(), glb.output.data(), glb.output.size());
        return out;
    }
}