#include "FBXAnimationFile.h"
#include "IBasicAnimation.h"
#include "FileManager.h"
#include "ClipGenerator.h"
#include "Util/String.h"
#include "Settings/Settings.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/simd_quaternion.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/animation/offline/tools/import2ozz.h>
#include <ozz/animation/offline/animation_builder.h>

#ifdef _WIN32
#include <excpt.h>
#endif

namespace Animation
{
    static ozz::animation::offline::fbx::FbxManagerInstance& GetFbxManager()
    {
        static ozz::animation::offline::fbx::FbxManagerInstance instance;
        return instance;
    }

    void FBXAnimationFile::CollectJointNames(const ozz::animation::offline::RawSkeleton::Joint& joint, std::vector<std::string>& out)
    {
        out.push_back(joint.name.c_str());
        for (const auto& child : joint.children)
            CollectJointNames(child, out);
    }

    // --- FBX settings (loaded from INI once) --------------------------------
    // Single flag: axis conversion mode. Everything else (rests fresh per
    // play, NAF writer, no trims) is fixed by design.
    static std::atomic<bool> g_fbxSettingsLoaded{ false };
    static std::atomic<int> g_fbxConvertMode{ 0 };  // 0=Rx+90 (Y->Z), 1=Rx-90, 2=identity
    static std::atomic<bool> g_fbxFollow{ true };    // follow dance travel via pelvis delta
    static std::atomic<bool> g_fbxTravelSnap{ true };  // snap travel target to walkable navmesh (collision + floor Z)
    static std::atomic<float> g_fbxLimbTrim[9][3];  // thighL/thighR/clavL/clavR/feet/spine/pelvis/calfL/calfR
    static std::vector<RawOzzAnimation::RootMotionKey> s_lastRootKeys;
    static float s_lastRootDur = 0.f;
    static std::string s_lastRootPath;
    static std::atomic<float> g_fbxHipsYawDeg{ 0.f };     // legacy Hips yaw correction (deg) — off, FrameCorr replaces it
    static std::atomic<int> g_fbxHipsYawMode{ 0 };        // 0=off, 1=file-side whole body, 2=game-side legs only
    static std::atomic<float> g_fbxSpineEulerDeg[3];     // spine pitch correction (deg XYZ, C_Spine once)
    static std::atomic<int> g_fbxLegsFix{ 0 };       // 0=off, 1=yaw-180 Hips (legs join torso)
    static std::atomic<int> g_fbxArmsFix{ 0 };       // 0=off, 1=Rx-180 clavicles, 2=Ry-180, 3=Rz-180
    static std::atomic<int> g_fbxRestAbs{ 0 };       // 0=rest-realign (delta), 1=absolute pose copy (rest:=bind)

    static std::string Trim(std::string_view a_text)
    {
        size_t start = 0;
        while (start < a_text.size() && std::isspace(static_cast<unsigned char>(a_text[start]))) ++start;
        size_t end = a_text.size();
        while (end > start && std::isspace(static_cast<unsigned char>(a_text[end - 1]))) --end;
        return std::string(a_text.substr(start, end - start));
    }

    static bool ParseEulerDeg(std::string_view a_value, float a_e[3])
    {
        std::string v(a_value);
        a_e[0] = a_e[1] = a_e[2] = 0.f;
        try {
            size_t p0 = 0;
            for (int k = 0; k < 3; ++k) {
                size_t p1 = v.find(',', p0);
                std::string part = (p1 == std::string::npos) ? v.substr(p0) : v.substr(p0, p1 - p0);
                a_e[k] = std::stof(std::string(Trim(part)));
                if (p1 == std::string::npos) break;
                p0 = p1 + 1;
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    static bool ParseBool(std::string_view a_text, bool a_default)
    {
        auto v = Trim(a_text);
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
        if (v == "0" || v == "false" || v == "no" || v == "off") return false;
        return a_default;
    }

    static void LoadFBXSettingsFromIni()
    {
        if (g_fbxSettingsLoaded.load(std::memory_order_acquire)) return;
        if (g_fbxSettingsLoaded.exchange(true, std::memory_order_acq_rel)) return;

        const auto iniPath = Util::String::GetDataPath() / "SFSE" / "Plugins" / "StarfieldAnimationFramework.ini";
        std::ifstream file(iniPath);
        if (!file.is_open()) {
            SAF_LOG_INFO("[FBX] StarfieldAnimationFramework.ini not found at {}, using defaults", iniPath.string());
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            const auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            auto key = Trim(std::string_view(line).substr(0, pos));
            auto value = Trim(std::string_view(line).substr(pos + 1));

            if (key == "FBXConvertMode") {
                try {
                    int v = std::stoi(std::string(value));
                    if (v < 0) v = 0;
                    if (v > 2) v = 2;
                    g_fbxConvertMode.store(v, std::memory_order_release);
                    SAF_LOG_INFO("[FBX] FBXConvertMode: {} (0=Rx+90, 1=Rx-90, 2=identity)", v);
                } catch (...) {
                    SAF_LOG_WARN("[FBX] FBXConvertMode invalid in ini: {}", value);
                }
            } else if (key == "FBXFollowActor") {
                const bool v = ParseBool(value, true);
                g_fbxFollow.store(v, std::memory_order_release);
                SAF_LOG_INFO("[FBX] FBXFollowActor: {} (pelvis delta moves data.location)", v ? "true" : "false");
            } else if (key == "FBXTravelNavmeshSnap") {
                const bool v = ParseBool(value, true);
                g_fbxTravelSnap.store(v, std::memory_order_release);
                SAF_LOG_INFO("[FBX] FBXTravelNavmeshSnap: {} (travel target snapped onto nearest walkable navmesh: walls/furniture block, Z glued to floor; 0 = raw move, no collision)", v ? "true" : "false");
            } else if (key == "FBXHipsYawDegrees") {
                try {
                    float v = std::stof(std::string(value));
                    if (v < -180.f) v = -180.f;
                    if (v > 180.f) v = 180.f;
                    g_fbxHipsYawDeg.store(v, std::memory_order_release);
                    SAF_LOG_INFO("[FBX] FBXHipsYawDegrees: {:.1f}", v);
                } catch (...) {
                    SAF_LOG_WARN("[FBX] FBXHipsYawDegrees invalid in ini: {}", value);
                }
            } else if (key == "FBXHipsYawMode") {
                try {
                    int v = std::stoi(std::string(value));
                    if (v < 0) v = 0;
                    if (v > 2) v = 2;
                    g_fbxHipsYawMode.store(v, std::memory_order_release);
                    SAF_LOG_INFO("[FBX] FBXHipsYawMode: {} (0=off, 1=file-side whole, 2=game-side legs)", v);
                } catch (...) {
                    SAF_LOG_WARN("[FBX] FBXHipsYawMode invalid in ini: {}", value);
                }
            } else if (key == "FBXSpineEuler") {
                float e[3] = { 0.f, 0.f, 0.f };
                if (ParseEulerDeg(value, e)) {
                    for (int k = 0; k < 3; ++k)
                        g_fbxSpineEulerDeg[k].store(e[k], std::memory_order_release);
                    SAF_LOG_INFO("[FBX] FBXSpineEuler: {:.1f},{:.1f},{:.1f} (C_Spine once)", e[0], e[1], e[2]);
                } else {
                    SAF_LOG_WARN("[FBX] FBXSpineEuler invalid in ini (want x,y,z): {}", value);
                }
            } else if (key == "FBXLegsFix") {
                try {
                    int v = std::stoi(std::string(value));
                    if (v < 0) v = 0;
                    if (v > 1) v = 1;
                    g_fbxLegsFix.store(v, std::memory_order_release);
                    SAF_LOG_INFO("[FBX] FBXLegsFix: {} (0=off, 1=yaw-180 Hips)", v);
                } catch (...) {
                    SAF_LOG_WARN("[FBX] FBXLegsFix invalid in ini: {}", value);
                }
            } else if (key == "FBXArmsFix") {
                try {
                    int v = std::stoi(std::string(value));
                    if (v < 0) v = 0;
                    if (v > 3) v = 3;
                    g_fbxArmsFix.store(v, std::memory_order_release);
                    SAF_LOG_INFO("[FBX] FBXArmsFix: {} (0=off, 1=Rx180, 2=Ry180, 3=Rz180 clavicles)", v);
                } catch (...) {
                    SAF_LOG_WARN("[FBX] FBXArmsFix invalid in ini: {}", value);
                }
            } else if (key == "FBXRestAbs") {
                try {
                    int v = std::stoi(std::string(value));
                    g_fbxRestAbs.store(v != 0 ? 1 : 0, std::memory_order_release);
                    SAF_LOG_INFO("[FBX] FBXRestAbs: {} (0=rest-realign delta, 1=absolute pose copy)", v);
                } catch (...) {
                    SAF_LOG_WARN("[FBX] FBXRestAbs invalid in ini: {}", value);
                }
            } else if (key == "FBXTrimSpine") {
                float e[3] = { 0.f, 0.f, 0.f };
                if (ParseEulerDeg(value, e)) {
                    g_fbxLimbTrim[5][0].store(e[0], std::memory_order_release);
                    g_fbxLimbTrim[5][1].store(e[1], std::memory_order_release);
                    g_fbxLimbTrim[5][2].store(e[2], std::memory_order_release);
                    SAF_LOG_INFO("[FBX] FBXTrimSpine default: {:.1f},{:.1f},{:.1f}", e[0], e[1], e[2]);
                } else {
                    SAF_LOG_WARN("[FBX] FBXTrimSpine invalid in ini (want x,y,z): {}", value);
                }
            } else if (key == "FBXTrimPelvis") {
                float e[3] = { 0.f, 0.f, 0.f };
                if (ParseEulerDeg(value, e)) {
                    g_fbxLimbTrim[6][0].store(e[0], std::memory_order_release);
                    g_fbxLimbTrim[6][1].store(e[1], std::memory_order_release);
                    g_fbxLimbTrim[6][2].store(e[2], std::memory_order_release);
                    SAF_LOG_INFO("[FBX] FBXTrimPelvis default: {:.1f},{:.1f},{:.1f}", e[0], e[1], e[2]);
                } else {
                    SAF_LOG_WARN("[FBX] FBXTrimPelvis invalid in ini (want x,y,z): {}", value);
                }
            }
        }
    }

    int FBXAnimationFile::GetConvertMode()
    {
        LoadFBXSettingsFromIni();
        return g_fbxConvertMode.load(std::memory_order_acquire);
    }

    bool FBXAnimationFile::GetFollowActor()
    {
        LoadFBXSettingsFromIni();
        return g_fbxFollow.load(std::memory_order_acquire);
    }

    bool FBXAnimationFile::GetTravelNavmeshSnap()
    {
        LoadFBXSettingsFromIni();
        return g_fbxTravelSnap.load(std::memory_order_acquire);
    }

    bool FBXAnimationFile::GetRestAbs()
    {
        LoadFBXSettingsFromIni();
        return g_fbxRestAbs.load(std::memory_order_acquire) != 0;
    }

    void FBXAnimationFile::SetRestAbs(bool a_abs)
    {
        g_fbxRestAbs.store(a_abs ? 1 : 0, std::memory_order_release);
        SAF_LOG_INFO("[FBX] RestAbs set live: {} (0=rest-realign delta, 1=absolute pose copy)", a_abs ? 1 : 0);
    }

    bool FBXAnimationFile::GetRootTravel(const std::string& a_baseName,
        std::vector<RawOzzAnimation::RootMotionKey>& a_keysOut, float& a_durOut)
    {
        const bool hit = (s_lastRootPath == a_baseName && !s_lastRootKeys.empty() && s_lastRootDur > 0.f);
        SAF_LOG_INFO("[FBX] GetRootTravel: want='{}' have='{}' keys={} -> {}",
            a_baseName, s_lastRootPath, s_lastRootKeys.size(), hit ? "HIT" : "MISS");
        if (!hit)
            return false;
        a_keysOut = s_lastRootKeys;
        a_durOut = s_lastRootDur;
        return true;
    }

    void FBXAnimationFile::SetLimbTrim(int a_part, float a_x, float a_y, float a_z)
    {
        if (a_part < 0 || a_part > 8) return;
        g_fbxLimbTrim[a_part][0].store(a_x, std::memory_order_release);
        g_fbxLimbTrim[a_part][1].store(a_y, std::memory_order_release);
        g_fbxLimbTrim[a_part][2].store(a_z, std::memory_order_release);
        SAF_LOG_INFO("[FBX] LimbTrim part={} set live: {:.1f},{:.1f},{:.1f}", a_part, a_x, a_y, a_z);
    }

    void FBXAnimationFile::GetLimbTrims(float a_out[9][3])
    {
        for (int p = 0; p < 9; ++p)
            for (int k = 0; k < 3; ++k)
                a_out[p][k] = g_fbxLimbTrim[p][k].load(std::memory_order_acquire);
    }

    // Convert a quaternion from Y-up to Z-up space (similarity/conjugate):
    // C * q * C^-1 with C = R_x(+90deg), which maps +Y to +Z.
    static ozz::math::Quaternion YUpToZUp(const ozz::math::Quaternion& q)
    {
        static const ozz::math::Quaternion kC(0.70710678f, 0.0f, 0.0f, 0.70710678f);
        return kC * q * ozz::math::Conjugate(kC);
    }

    // Convert a vector (translation) from Y-up to Z-up: v' = R_x(+90deg) * v → (x, -z, y).
    static ozz::math::Float3 YUpToZUpVec(const ozz::math::Float3& v)
    {
        return ozz::math::Float3(v.x, -v.z, v.y);
    }

    // Rx(-90deg) variant (ConvertMode 1): q' = C- * q * C-^-1, C- = R_x(-90deg).
    static ozz::math::Quaternion YUpToZUpNeg(const ozz::math::Quaternion& q)
    {
        static const ozz::math::Quaternion kCM(-0.70710678f, 0.0f, 0.0f, 0.70710678f);
        return kCM * q * ozz::math::Conjugate(kCM);
    }

    // Rx(-90deg) vector variant: v' = (x, z, -y).
    static ozz::math::Float3 YUpToZUpNegVec(const ozz::math::Float3& v)
    {
        return ozz::math::Float3(v.x, v.z, -v.y);
    }

    // Rotate a vector by a unit quaternion: v' = q * v * conj(q).
    static ozz::math::Float3 QuatRotateVec(const ozz::math::Quaternion& q, const ozz::math::Float3& v)
    {
        const ozz::math::Float3 u(q.x, q.y, q.z);
        const ozz::math::Float3 t(
            u.y * v.z - u.z * v.y,
            u.z * v.x - u.x * v.z,
            u.x * v.y - u.y * v.x);
        const ozz::math::Float3 tt(
            u.y * t.z - u.z * t.y,
            u.z * t.x - u.x * t.z,
            u.x * t.y - u.y * t.x);
        return ozz::math::Float3(
            v.x + 2.f * (q.w * t.x + tt.x),
            v.y + 2.f * (q.w * t.y + tt.y),
            v.z + 2.f * (q.w * t.z + tt.z));
    }

    // Strip common mixamo prefixes like "mixamorig:" or "mixamo:"
    static std::string StripMixamoPrefix(const std::string& name)
    {
        const char* prefixes[] = { "mixamorig:", "mixamo:" };
        for (auto* pfx : prefixes)
        {
            size_t plen = std::strlen(pfx);
            if (name.size() > plen && name.compare(0, plen, pfx) == 0)
                return name.substr(plen);
        }
        return name;
    }

    // --- Minimal live rest capture (NiNode reads, self-contained) ------------
    // Fresh capture every play: no cache, so a poisoned rest can never persist.
    // All risky reads go through POD-only helpers (SEH-safe, C2712-clean).
    static bool Fbx2NodeName(RE::NiAVObject* a_node, char* a_buf, size_t a_n)
    {
        if (!a_buf || a_n == 0) return false;
        a_buf[0] = '\0';
        if (!a_node) return false;
        __try {
            const char* n = a_node->name.c_str();
            if (!n) return false;
            strncpy_s(a_buf, a_n, n, _TRUNCATE);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static RE::NiNode* Fbx2GetParent(RE::NiAVObject* a_node)
    {
        if (!a_node) return nullptr;
        __try { return a_node->parent; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // Direct typed read of the node's own local transform (same ABI access
    // pattern as Settings::CaptureSkeletonNIFRest). POD-only body so the SEH
    // wrapper stays C2712-clean.
    static bool Fbx2ReadLocalSafe(RE::NiAVObject* a_node, float a_out7[7])
    {
        if (!a_node || !a_out7) return false;
        __try {
            RE::NiQuaternion rq(a_node->local.rotate);
            a_out7[0] = rq.x; a_out7[1] = rq.y; a_out7[2] = rq.z; a_out7[3] = rq.w;
            a_out7[4] = a_node->local.translate.x;
            a_out7[5] = a_node->local.translate.y;
            a_out7[6] = a_node->local.translate.z;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static void Fbx2Mat3ToQuat(const float m[9], float& qx, float& qy, float& qz, float& qw)
    {
        const float t = m[0] + m[4] + m[8];
        if (t > 0.0f) {
            float s = 0.5f / std::sqrt(t + 1.0f);
            qw = 0.25f / s;
            qx = (m[7] - m[5]) * s;
            qy = (m[2] - m[6]) * s;
            qz = (m[3] - m[1]) * s;
        } else if (m[0] > m[4] && m[0] > m[8]) {
            float s = 2.0f * std::sqrt(1.0f + m[0] - m[4] - m[8]);
            qw = (m[7] - m[5]) / s;
            qx = 0.25f * s;
            qy = (m[1] + m[3]) / s;
            qz = (m[2] + m[6]) / s;
        } else if (m[4] > m[8]) {
            float s = 2.0f * std::sqrt(1.0f + m[4] - m[0] - m[8]);
            qw = (m[2] - m[6]) / s;
            qx = (m[1] + m[3]) / s;
            qy = 0.25f * s;
            qz = (m[5] + m[7]) / s;
        } else {
            float s = 2.0f * std::sqrt(1.0f + m[8] - m[0] - m[4]);
            qw = (m[3] - m[1]) / s;
            qx = (m[2] + m[6]) / s;
            qy = (m[5] + m[7]) / s;
            qz = 0.25f * s;
        }
    }

    static void Fbx2NormalizeInPlace(char* a_s)
    {
        if (!a_s) return;
        char* w = a_s;
        for (const char* r = a_s; *r; ++r) {
            char c = *r;
            if (c == ' ' || c == '\t') continue;
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            *w++ = c;
        }
        *w = '\0';
        if (w - a_s > 3 && a_s[0] == 'n' && a_s[1] == 'p' && a_s[2] == 'c') {
            size_t n = static_cast<size_t>(w - a_s) - 3;
            std::memmove(a_s, a_s + 3, n + 1);
        }
    }

    static bool Fbx2CaptureRest(RE::Actor* a_actor, const std::vector<std::string>& a_names,
        std::vector<ozz::math::Quaternion>& a_rots, std::vector<ozz::math::Float3>& a_trans, std::vector<int16_t>& a_parents)
    {
        a_rots.clear(); a_trans.clear(); a_parents.clear();
        if (!a_actor || a_names.empty()) return false;
        const size_t nJ = a_names.size();

        RE::NiAVObject* root = nullptr;
        {
            auto loaded = a_actor->loadedData.LockRead();
            auto* lp = *loaded;
            root = (lp && lp->data3D) ? lp->data3D.get() : nullptr;
        }
        if (!root) {
            SAF_LOG_WARN("[FBX] CaptureRest: actor has no 3D");
            return false;
        }

        std::vector<RE::NiAVObject*> nodes(nJ, nullptr);
        for (size_t i = 0; i < nJ; ++i) {
            const std::string& nm = a_names[i];
            if (nm.empty()) continue;
            RE::NiAVObject* found = root->GetObjectByName(RE::BSFixedString(nm.c_str()));
            if (!found) {
                std::string spaced;
                spaced.reserve(nm.size() + 4);
                for (char c : nm) spaced.push_back(c == '_' ? ' ' : c);
                std::string npcName = "NPC " + spaced;
                found = root->GetObjectByName(RE::BSFixedString(npcName.c_str()));
            }
            if (!found) {
                std::string npcOrig = "NPC " + nm;
                found = root->GetObjectByName(RE::BSFixedString(npcOrig.c_str()));
            }
            if (!found) {
                char norm[256];
                strncpy_s(norm, sizeof(norm), nm.c_str(), _TRUNCATE);
                Fbx2NormalizeInPlace(norm);
                if (norm[0]) {
                    found = root->GetObjectByName(RE::BSFixedString(norm));
                    if (!found) {
                        std::string npcNorm = std::string("NPC ") + norm;
                        found = root->GetObjectByName(RE::BSFixedString(npcNorm.c_str()));
                    }
                }
            }
            nodes[i] = found;
        }
        if (nJ > 0 && !nodes[0])
            nodes[0] = root;

        std::unordered_map<std::string, int16_t> nameToIdx;
        for (size_t i = 0; i < nJ; ++i) {
            if (a_names[i].empty()) continue;
            char nb[256];
            strncpy_s(nb, sizeof(nb), a_names[i].c_str(), _TRUNCATE);
            Fbx2NormalizeInPlace(nb);
            if (nb[0]) nameToIdx.emplace(nb, static_cast<int16_t>(i));
        }

        a_rots.assign(nJ, ozz::math::Quaternion::identity());
        a_trans.assign(nJ, ozz::math::Float3::zero());
        a_parents.assign(nJ, -1);
        int foundN = 0, linkedP = 0;
        for (size_t i = 0; i < nJ; ++i) {
            if (!nodes[i]) continue;
            float loc[7];
            if (!Fbx2ReadLocalSafe(nodes[i], loc)) continue;
            a_rots[i] = ozz::math::Quaternion(loc[0], loc[1], loc[2], loc[3]);
            a_trans[i] = ozz::math::Float3(loc[4], loc[5], loc[6]);
            ++foundN;
            RE::NiNode* par = Fbx2GetParent(nodes[i]);
            if (par) {
                char pb[256];
                if (Fbx2NodeName(par, pb, sizeof(pb))) {
                    Fbx2NormalizeInPlace(pb);
                    auto it = nameToIdx.find(pb);
                    if (it != nameToIdx.end() && it->second != static_cast<int16_t>(i)) {
                        a_parents[i] = it->second;
                        ++linkedP;
                    }
                }
            }
        }
        if (nJ > 0)
            a_rots[0] = ozz::math::Quaternion::identity();
        auto qang = [](ozz::math::Quaternion q) {
            float len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
            if (len > 1e-8f) q.w /= len;
            return 2.0f * std::acos(std::min(1.0f, std::abs(q.w))) * 57.29578f;
        };
        int hipsGi = -1, spineGi = -1;
        for (size_t i = 0; i < nJ; ++i) {
            if (a_names[i] == "C_Hips") hipsGi = static_cast<int>(i);
            if (a_names[i] == "C_Spine") spineGi = static_cast<int>(i);
        }
        SAF_LOG_INFO("[FBX] CaptureRest: nodes={}/{} parents={} Hips={:.1f}deg Spine={:.1f}deg",
            foundN, nJ, linkedP,
            hipsGi >= 0 ? qang(a_rots[hipsGi]) : -1.f,
            spineGi >= 0 ? qang(a_rots[spineGi]) : -1.f);
        return foundN > 0;
    }

    std::unique_ptr<RawOzzAnimation> FBXAnimationFile::BuildWorldClip(
        const std::string& a_filePath, std::shared_ptr<OzzSkeleton> a_gameSkeleton, RE::Actor* a_actor)
    {
        LoadFBXSettingsFromIni();
        if (!a_gameSkeleton || !a_gameSkeleton->data || !a_actor)
            return nullptr;

        SAF_LOG_INFO("[FBX] build tag: {} {} (world formula, fresh rest, NAF writer)", __DATE__, __TIME__);

        ozz::animation::offline::fbx::FbxDefaultIOSettings io_settings(GetFbxManager());
        ozz::animation::offline::fbx::FbxSceneLoader loader(a_filePath.c_str(), nullptr, GetFbxManager(), io_settings);
        if (!loader.scene())
            return nullptr;

        ozz::animation::offline::RawSkeleton fbx_raw_sk;
        ozz::animation::offline::OzzImporter::NodeType node_types;
        node_types.skeleton = true;
        node_types.marker = false;
        node_types.camera = false;
        node_types.geometry = false;
        node_types.light = false;
        node_types.null = false;
        node_types.any = false;
        if (!ozz::animation::offline::fbx::ExtractSkeleton(loader, node_types, &fbx_raw_sk))
            return nullptr;

        ozz::animation::offline::SkeletonBuilder skel_builder;
        auto fbx_ozz_skel = skel_builder(fbx_raw_sk);
        if (!fbx_ozz_skel)
            return nullptr;

        std::vector<std::string> fbx_joint_names_raw;
        for (const auto& root : fbx_raw_sk.roots)
            CollectJointNames(root, fbx_joint_names_raw);
        std::vector<std::string> fbx_joint_names;
        fbx_joint_names.reserve(fbx_joint_names_raw.size());
        for (auto& n : fbx_joint_names_raw)
            fbx_joint_names.push_back(StripMixamoPrefix(n));

        auto anim_names = ozz::animation::offline::fbx::GetAnimationNames(loader);
        if (anim_names.empty())
            return nullptr;

        ozz::animation::offline::RawAnimation raw_anim_fbx;
        if (!ozz::animation::offline::fbx::ExtractAnimation(anim_names[0].c_str(), loader, *fbx_ozz_skel, 0.0f, &raw_anim_fbx))
            return nullptr;
        if (raw_anim_fbx.tracks.size() != fbx_joint_names.size()) {
            SAF_LOG_WARN("[FBX] BuildWorld: track/joint mismatch (tracks={}, joints={})",
                raw_anim_fbx.tracks.size(), fbx_joint_names.size());
            return nullptr;
        }
        {
            auto wantTrack = [](const std::string& nm) {
                static const char* k_keys[] = { "Hips", "Spine", "Neck", "Head", "Shoulder",
                    "Arm", "Hand", "UpLeg", "Leg", "Foot", "Toe" };
                for (const char* k : k_keys)
                    if (nm.find(k) != std::string::npos) return true;
                return false;
            };
            for (size_t f = 0; f < fbx_joint_names.size(); ++f) {
                const auto& nm = fbx_joint_names[f];
                if (wantTrack(nm)) {
                    const auto& tr = raw_anim_fbx.tracks[f];
                    const float t0 = tr.rotations.empty() ? -1.f : tr.rotations.front().time;
                    const float t1 = tr.rotations.empty() ? -1.f : tr.rotations.back().time;
                    SAF_LOG_INFO("[FBX] Track '{}': rotKeys={} transKeys={} t=[{:.2f},{:.2f}] dur={:.2f}",
                        nm, tr.rotations.size(), tr.translations.size(), t0, t1, raw_anim_fbx.duration);
                }
            }
        }

        const auto& skeleton = a_gameSkeleton;
        const size_t nJ = skeleton->jointNames.size();
        const auto& boneAliases = Settings::GetFBXBoneAliases();
        std::vector<int> gameToFbx(nJ, -1);
        for (size_t i = 0; i < nJ; ++i) {
            const std::string& gameName = skeleton->jointNames[i];
            for (size_t f = 0; f < fbx_joint_names.size(); ++f) {
                bool hit = (fbx_joint_names[f] == gameName);
                if (!hit) {
                    auto ait = boneAliases.find(fbx_joint_names[f]);
                    hit = (ait != boneAliases.end() && ait->second == gameName);
                }
                if (!hit && i < skeleton->jointAliases.size()) {
                    const auto& al = skeleton->jointAliases[i];
                    hit = (std::find(al.begin(), al.end(), fbx_joint_names[f]) != al.end());
                }
                if (hit) { gameToFbx[i] = static_cast<int>(f); break; }
            }
        }
        {
            int mappedN = 0;
            std::string unmapped;
            for (size_t i = 0; i < nJ; ++i) {
                if (gameToFbx[i] >= 0) { ++mappedN; continue; }
                if (unmapped.size() < 1000) {
                    if (!unmapped.empty()) unmapped += ", ";
                    unmapped += skeleton->jointNames[i];
                }
            }
            SAF_LOG_INFO("[FBX] Mapping: {}/{} game joints -> FBX tracks; unmapped: [{}]",
                mappedN, nJ, unmapped);
        }

        // C_Waist follows the pelvis file joint (unfreezes the torso pivot;
        // aliases are 1:1 so this cannot be expressed in FBXBoneAliases.ini).
        for (size_t i = 0; i < nJ; ++i) {
            if (skeleton->jointNames[i] == "C_Waist" && gameToFbx[i] < 0) {
                for (size_t f = 0; f < fbx_joint_names.size(); ++f) {
                    if (fbx_joint_names[f] == "Hips") {
                        gameToFbx[i] = static_cast<int>(f);
                        SAF_LOG_INFO("[FBX] Mapping: C_Waist -> Hips (pelvis follow)");
                        break;
                    }
                }
            }
        }

        // Helper deform bones follow their animated neighbor's file joint
        // (full world copy): with rigid parent-follow the shoulder/knee/neck
        // flesh stays behind when the limb moves (visible as stretching).
        {
            static const struct { const char* game; const char* file; } kHelperMap[] = {
                { "L_Deltoid", "LeftArm" }, { "R_Deltoid", "RightArm" },
                { "L_ArmMass", "LeftArm" }, { "R_ArmMass", "RightArm" },
                { "L_Elbow", "LeftForeArm" }, { "R_Elbow", "RightForeArm" },
                { "L_Knee", "LeftLeg" }, { "R_Knee", "RightLeg" },
                { "C_Neck1", "Neck" },
            };
            for (size_t i = 0; i < nJ; ++i) {
                if (gameToFbx[i] >= 0) continue;
                for (const auto& hm : kHelperMap) {
                    if (skeleton->jointNames[i] != hm.game) continue;
                    for (size_t f = 0; f < fbx_joint_names.size(); ++f) {
                        if (fbx_joint_names[f] == hm.file) {
                            gameToFbx[i] = static_cast<int>(f);
                            SAF_LOG_INFO("[FBX] Mapping: {} -> {} (helper follow)", hm.game, hm.file);
                            break;
                        }
                    }
                    break;
                }
            }
        }

        int pelvisBoneIdx = -1;
        for (size_t j = 0; j < nJ; ++j) {
            const std::string& gn = skeleton->jointNames[j];
            if (gn == "C_Hips") { pelvisBoneIdx = static_cast<int>(j); break; }
        }
        int waistBoneIdx = -1;
        for (size_t j = 0; j < nJ; ++j) {
            if (skeleton->jointNames[j] == "C_Waist") { waistBoneIdx = static_cast<int>(j); break; }
        }
        int spineBoneIdx = -1;
        for (size_t j = 0; j < nJ; ++j) {
            if (skeleton->jointNames[j] == "C_Spine") { spineBoneIdx = static_cast<int>(j); break; }
        }
        const bool restAbs = g_fbxRestAbs.load(std::memory_order_acquire) != 0;

        const int convMode = g_fbxConvertMode.load(std::memory_order_acquire);
        const int legsFix = g_fbxLegsFix.load(std::memory_order_acquire);
        const int armsFix = g_fbxArmsFix.load(std::memory_order_acquire);
        auto convR = [convMode](const ozz::math::Quaternion& q) {
            if (convMode == 1) return YUpToZUpNeg(q);
            if (convMode == 2) return q;
            return YUpToZUp(q);
        };
        auto convV = [convMode](const ozz::math::Float3& v) {
            if (convMode == 1) return YUpToZUpNegVec(v);
            if (convMode == 2) return v;
            return YUpToZUpVec(v);
        };

        // Unit scale: file may be cm (Hips ~104) or m (Hips ~1). Rotations unaffected.
        float unitScale = 1.f;
        {
            int hipsFi = -1;
            for (size_t f = 0; f < fbx_joint_names.size(); ++f)
                if (fbx_joint_names[f] == "Hips") { hipsFi = static_cast<int>(f); break; }
            if (hipsFi >= 0 && static_cast<size_t>(hipsFi) < raw_anim_fbx.tracks.size() &&
                !raw_anim_fbx.tracks[hipsFi].translations.empty()) {
                const auto& t0 = raw_anim_fbx.tracks[hipsFi].translations.front().value;
                const float m = std::sqrt(t0.x * t0.x + t0.y * t0.y + t0.z * t0.z);
                if (m > 10.f) unitScale = 0.01f;
                SAF_LOG_INFO("[FBX] BuildWorld: Hips first-key |T|={:.2f} -> unitScale={} file='{}'",
                    m, unitScale, a_filePath);
            }
        }

        // Hips yaw + spine euler corrections (baked once per load).
        // Yaw: mode 1 = file-side (whole body turns, spine inherits through
        // the file chain), mode 2 = game-side pelvis result only (legs join
        // the torso, game COM siblings). Euler XYZ once at C_Spine.
        int fbxHipsIdx = -1;
        for (size_t f = 0; f < fbx_joint_names.size(); ++f)
            if (fbx_joint_names[f] == "Hips") { fbxHipsIdx = static_cast<int>(f); break; }
        const float yawDeg = g_fbxHipsYawDeg.load(std::memory_order_acquire);
        const int yawMode = g_fbxHipsYawMode.load(std::memory_order_acquire);
        ozz::math::Quaternion yawQ = ozz::math::Quaternion::identity();
        if (yawMode != 0 && std::abs(yawDeg) > 0.01f) {
            const float a = yawDeg * 0.0174533f * 0.5f;
            yawQ = ozz::math::Quaternion(0.f, 0.f, std::sin(a), std::cos(a));
        }
        float eux = 0.f, euy = 0.f, euz = 0.f;
        eux = g_fbxSpineEulerDeg[0].load(std::memory_order_acquire);
        euy = g_fbxSpineEulerDeg[1].load(std::memory_order_acquire);
        euz = g_fbxSpineEulerDeg[2].load(std::memory_order_acquire);
        ozz::math::Quaternion spineE = ozz::math::Quaternion::identity();
        bool haveSpineE = false;
        if (eux != 0.f || euy != 0.f || euz != 0.f) {
            const float hx = eux * 0.00872665f, hy = euy * 0.00872665f, hz = euz * 0.00872665f;
            ozz::math::Quaternion qx(std::sin(hx), 0.f, 0.f, std::cos(hx));
            ozz::math::Quaternion qy(0.f, std::sin(hy), 0.f, std::cos(hy));
            ozz::math::Quaternion qz(0.f, 0.f, std::sin(hz), std::cos(hz));
            spineE = (qx * qy) * qz;
            haveSpineE = true;
        }
        SAF_LOG_INFO("[FBX] Corrections: yaw={:.1f} mode={} (0=off,1=file whole,2=game legs) euler={:.1f},{:.1f},{:.1f}",
            yawDeg, yawMode, eux, euy, euz);

        // Game rest: race skeleton NIF bind pose first (static, actor-
        // independent). A live actor is never at rest — the NAF writer leaves
        // the last animated pose in the nodes, which would bake into every
        // clip frame. Live capture stays as fallback when the NIF is missing.
        std::vector<ozz::math::Quaternion> gameRestRotations;
        std::vector<ozz::math::Float3> gameRestTranslations;
        std::vector<int16_t> gameParents;
        bool restFromNif = false;
        if (auto* npc = a_actor->GetNPC()) {
            if (auto* race = npc->GetRace())
                restFromNif = Settings::CaptureSkeletonNIFRest(
                    race, skeleton->jointNames, gameRestRotations, gameRestTranslations, gameParents);
        }
        if (!restFromNif) {
            SAF_LOG_WARN("[FBX] BuildWorld: NIF rest unavailable, falling back to live actor capture");
            if (!Fbx2CaptureRest(a_actor, skeleton->jointNames, gameRestRotations, gameRestTranslations, gameParents)) {
                SAF_LOG_ERROR("[FBX] BuildWorld: rest capture failed for '{}'", a_filePath);
                return nullptr;
            }
        }

        // True bind overlay: live capture is polluted — a live actor is never
        // at rest, the NAF writer leaves the last animated pose in the nodes
        // (arms droop ~37deg into idle/profile). Overlay rotations from
        // <race>.bind.json (true bind from the GLB pipeline) onto the captured
        // rest so conjugation is self-consistent. Parents and translations
        // stay captured (game hierarchy + measured positions).
        bool bindJsonApplied = false;
        if (!restFromNif) {
            if (auto* npc = a_actor->GetNPC()) {
                if (auto* race = npc->GetRace()) {
                    const char* rid = race->formEditorID.c_str();
                    if (rid && *rid)
                        bindJsonApplied =
                            Settings::OverlaySkeletonBindJson(rid, skeleton->jointNames, gameRestRotations) > 0;
                }
            }
        }

        // File rest pose (converted), indexed by file DFS order.
        std::vector<ozz::math::Quaternion> fbxRestRotations(fbx_joint_names.size());
        std::vector<ozz::math::Quaternion> fbxRestWorld(fbx_joint_names.size());
        std::vector<ozz::math::Float3> fbxRestTranslations(fbx_joint_names.size());
        std::vector<int16_t> fbxParents(fbx_joint_names.size(), -1);
        {
            struct RestWalker {
                std::vector<ozz::math::Quaternion>& localOut;
                std::vector<ozz::math::Quaternion>& worldOut;
                std::vector<ozz::math::Float3>& transOut;
                std::vector<int16_t>& parents;
                size_t idx = 0;
                void Visit(const ozz::animation::offline::RawSkeleton::Joint& j, int16_t parentIdx, const ozz::math::Quaternion& parentWorld) {
                    size_t myIdx = idx++;
                    parents[myIdx] = parentIdx;
                    localOut[myIdx] = j.transform.rotation;
                    transOut[myIdx] = j.transform.translation;
                    worldOut[myIdx] = parentWorld * j.transform.rotation;
                    for (auto& c : j.children) Visit(c, static_cast<int16_t>(myIdx), worldOut[myIdx]);
                }
            };
            RestWalker rw{ fbxRestRotations, fbxRestWorld, fbxRestTranslations, fbxParents, 0 };
            for (const auto& root : fbx_raw_sk.roots)
                rw.Visit(root, -1, ozz::math::Quaternion::identity());
        }
        for (auto& wq : fbxRestWorld)
            wq = convR(wq);

        // Game world rest rotations (multi-pass, root-normalized).
        std::vector<ozz::math::Quaternion> gameRestWorld(nJ, ozz::math::Quaternion::identity());
        {
            for (int pass = 0; pass < static_cast<int>(nJ); ++pass) {
                bool changed = false;
                for (size_t i = 0; i < nJ; ++i) {
                    if (i == 0) {
                        gameRestWorld[i] = ozz::math::Quaternion::identity();
                        continue;
                    }
                    const int16_t p = gameParents[i];
                    if (p >= 0 && static_cast<size_t>(p) < nJ && p != static_cast<int16_t>(i)) {
                        const ozz::math::Quaternion w = gameRestWorld[p] * gameRestRotations[i];
                        if (w.x != gameRestWorld[i].x || w.y != gameRestWorld[i].y ||
                            w.z != gameRestWorld[i].z || w.w != gameRestWorld[i].w) {
                            gameRestWorld[i] = w; changed = true;
                        }
                    } else {
                        gameRestWorld[i] = gameRestRotations[i];
                    }
                }
                if (!changed) break;
            }
        }

        // Spine ride: in the game skeleton C_Spine is a SIBLING of C_Hips
        // (both parented to the armature at ~1m height — biped layout), while
        // in the file the spine chain hangs off Hips and inherits all pelvis
        // motion. The translation writer must feed the pelvis drive into
        // C_Spine, or the belly/chest/neck/head/arms stay pinned to the
        // armature while the pelvis+legs bob/sway — visible as stretch and
        // "no up-down" above the waist.
        const int spineFIdx = (spineBoneIdx >= 0) ? gameToFbx[spineBoneIdx] : -1;
        const int hipsFIdx = (pelvisBoneIdx >= 0) ? gameToFbx[pelvisBoneIdx] : -1;
        const bool spineRide = spineBoneIdx >= 0 && pelvisBoneIdx >= 0 &&
            spineBoneIdx != pelvisBoneIdx && spineFIdx >= 0 && hipsFIdx >= 0 &&
            static_cast<size_t>(spineFIdx) < fbx_joint_names.size() &&
            static_cast<size_t>(hipsFIdx) < fbx_joint_names.size() &&
            gameParents[static_cast<size_t>(spineBoneIdx)] != static_cast<int16_t>(pelvisBoneIdx);
        ozz::math::Float3 spineOffG = ozz::math::Float3::zero();

        // Frame correction between the converted file space and the game
        // skeleton. Both humanoids stand upright after convR, but their
        // side/facing axes can differ by a constant rotation (Mixamo faces
        // -Y after Rx+90, Starfield faces +Y => Rz 180). Without it every
        // world delta lands in the wrong frame: legs swing backwards, arms
        // mirror left/right. Derived from rest POSITIONS (right-vs-left
        // thigh = side axis, hips->head = up axis), so no hand tuning per
        // source or skeleton.
        ozz::math::Quaternion frameCorr = ozz::math::Quaternion::identity();
        {
            auto qn = [](ozz::math::Quaternion q) {
                const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
                if (l > 1e-8f) { const float inv = 1.f / l; q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv; }
                return q;
            };
            auto findName = [](const std::vector<std::string>& names, const char* n) {
                for (size_t i = 0; i < names.size(); ++i)
                    if (names[i] == n) return static_cast<int>(i);
                return -1;
            };
            auto norm3 = [](const ozz::math::Float3& v) {
                const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                if (l < 1e-6f) return ozz::math::Float3::zero();
                return ozz::math::Float3(v.x / l, v.y / l, v.z / l);
            };
            auto cross3 = [](const ozz::math::Float3& a, const ozz::math::Float3& b) {
                return ozz::math::Float3(
                    a.y * b.z - a.z * b.y,
                    a.z * b.x - a.x * b.z,
                    a.x * b.y - a.y * b.x);
            };
            auto sub3 = [](const ozz::math::Float3& a, const ozz::math::Float3& b) {
                return ozz::math::Float3(a.x - b.x, a.y - b.y, a.z - b.z);
            };
            auto add3 = [](const ozz::math::Float3& a, const ozz::math::Float3& b) {
                return ozz::math::Float3(a.x + b.x, a.y + b.y, a.z + b.z);
            };
            auto isZero3 = [](const ozz::math::Float3& v) {
                return v.x == 0.f && v.y == 0.f && v.z == 0.f;
            };

            // File rest world positions (DFS order: parents first).
            std::vector<ozz::math::Float3> fbxRestPos(fbx_joint_names.size(), ozz::math::Float3::zero());
            for (size_t f = 0; f < fbx_joint_names.size(); ++f) {
                const int16_t p = fbxParents[f];
                const ozz::math::Float3 loc = convV(fbxRestTranslations[f]);
                if (p >= 0 && static_cast<size_t>(p) < f)
                    fbxRestPos[f] = add3(fbxRestPos[p], QuatRotateVec(fbxRestWorld[p], loc));
                else
                    fbxRestPos[f] = loc;
            }

            // Game rest world positions. gameParents comes from the capture
            // (node hierarchy), so a parent may sit after its child in the
            // runtime array — resolve with passes until stable.
            std::vector<ozz::math::Float3> gameRestPos(nJ, ozz::math::Float3::zero());
            {
                std::vector<char> done(nJ, 0);
                for (size_t i = 0; i < nJ; ++i) {
                    const int16_t p = gameParents[i];
                    const bool hasP = p >= 0 && static_cast<size_t>(p) < nJ && p != static_cast<int16_t>(i);
                    if (!hasP) {
                        gameRestPos[i] = QuatRotateVec(gameRestWorld[i], gameRestTranslations[i]);
                        done[i] = 1;
                    }
                }
                for (size_t pass = 0; pass < nJ; ++pass) {
                    bool all = true;
                    for (size_t i = 0; i < nJ; ++i) {
                        if (done[i]) continue;
                        const int16_t p = gameParents[i];
                        if (p >= 0 && static_cast<size_t>(p) < nJ &&
                            p != static_cast<int16_t>(i) && done[p]) {
                            gameRestPos[i] = add3(gameRestPos[p],
                                QuatRotateVec(gameRestWorld[p], gameRestTranslations[i]));
                            done[i] = 1;
                        } else {
                            all = false;
                        }
                    }
                    if (all) break;
                }
            }

            const int hipsF = findName(fbx_joint_names, "Hips");
            const int headF = findName(fbx_joint_names, "Head");
            const int lthF = findName(fbx_joint_names, "LeftUpLeg");
            const int rthF = findName(fbx_joint_names, "RightUpLeg");
            const int hipsG = findName(skeleton->jointNames, "C_Hips");
            const int headG = findName(skeleton->jointNames, "C_Head");
            const int lthG = findName(skeleton->jointNames, "L_Thigh");
            const int rthG = findName(skeleton->jointNames, "R_Thigh");
            auto okIdx = [](int i, size_t n) { return i >= 0 && static_cast<size_t>(i) < n; };
            const bool haveBasis =
                okIdx(hipsF, fbx_joint_names.size()) && okIdx(headF, fbx_joint_names.size()) &&
                okIdx(lthF, fbx_joint_names.size()) && okIdx(rthF, fbx_joint_names.size()) &&
                okIdx(hipsG, nJ) && okIdx(headG, nJ) && okIdx(lthG, nJ) && okIdx(rthG, nJ);
            if (haveBasis) {
                const ozz::math::Float3 upF = norm3(sub3(fbxRestPos[headF], fbxRestPos[hipsF]));
                const ozz::math::Float3 sideF = norm3(sub3(fbxRestPos[rthF], fbxRestPos[lthF]));
                ozz::math::Float3 upG = norm3(sub3(gameRestPos[headG], gameRestPos[hipsG]));
                ozz::math::Float3 sideG = norm3(sub3(gameRestPos[rthG], gameRestPos[lthG]));
                // The live capture can catch the actor mid-pose (the NAF
                // writer leaves the last animated pose in the nodes), which
                // corrupts the derived game basis. The standing humanoid
                // game frame is a constant of this skeleton: up=+Z,
                // side=+X. Validate the capture and fall back to that fixed
                // basis when it lies.
                if (upG.z < 0.7f) {
                    SAF_LOG_WARN("[FBX] FrameCorr: captured game rest is not standing "
                        "(upG.z={:.2f}, actor pose polluted by a previous animation) — using fixed game basis up=+Z side=+X",
                        upG.z);
                    upG = ozz::math::Float3(0.f, 0.f, 1.f);
                }
                if (std::abs(sideG.z) > 0.5f) {
                    sideG = ozz::math::Float3(1.f, 0.f, 0.f);
                }
                if (!isZero3(upF) && !isZero3(sideF) && !isZero3(upG) && !isZero3(sideG)) {
                    // fwd = side x up; whether it points front or back does
                    // not matter — it is derived identically in both spaces,
                    // and a rotation that maps side+up automatically maps
                    // their cross product.
                    const ozz::math::Float3 fwdF = cross3(sideF, upF);
                    const ozz::math::Float3 fwdG = cross3(sideG, upG);
                    // Columns [side, up, fwd]; M = Bg * Bf^T, row-major.
                    const float Bf[3][3] = {
                        { sideF.x, upF.x, fwdF.x },
                        { sideF.y, upF.y, fwdF.y },
                        { sideF.z, upF.z, fwdF.z } };
                    const float Bg[3][3] = {
                        { sideG.x, upG.x, fwdG.x },
                        { sideG.y, upG.y, fwdG.y },
                        { sideG.z, upG.z, fwdG.z } };
                    float m9[9];
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c) {
                            float s = 0.f;
                            for (int k = 0; k < 3; ++k) s += Bg[r][k] * Bf[c][k];
                            m9[r * 3 + c] = s;
                        }
                    float qx, qy, qz, qw;
                    Fbx2Mat3ToQuat(m9, qx, qy, qz, qw);
                    frameCorr = qn(ozz::math::Quaternion(qx, qy, qz, qw));
                }
                float ax = frameCorr.x, ay = frameCorr.y, az = frameCorr.z;
                const float al = std::sqrt(ax * ax + ay * ay + az * az);
                float ang = 2.f * std::acos(std::min(1.f, std::max(-1.f, frameCorr.w))) * 57.29578f;
                if (al > 1e-6f) { ax /= al; ay /= al; az /= al; } else { ang = 0.f; ax = 0.f; ay = 0.f; az = 1.f; }
                SAF_LOG_INFO("[FBX] FrameCorr: axis=({:.3f},{:.3f},{:.3f}) angle={:.1f}deg "
                    "upF=({:.2f},{:.2f},{:.2f}) sideF=({:.2f},{:.2f},{:.2f}) upG=({:.2f},{:.2f},{:.2f}) sideG=({:.2f},{:.2f},{:.2f})",
                    ax, ay, az, ang,
                    upF.x, upF.y, upF.z, sideF.x, sideF.y, sideF.z,
                    upG.x, upG.y, upG.z, sideG.x, sideG.y, sideG.z);
            } else {
                SAF_LOG_WARN("[FBX] FrameCorr: basis joints missing, correction disabled "
                    "(need Hips/Head/LeftUpLeg/RightUpLeg + C_Hips/C_Head/L_Thigh/R_Thigh)");
            }

            // Spine-ride offset: file-space hips->spine rest vector converted
            // to game axes and scale (frameCorr is final here).
            if (spineRide) {
                const auto offF = sub3(fbxRestPos[static_cast<size_t>(spineFIdx)],
                    fbxRestPos[static_cast<size_t>(hipsFIdx)]);
                const auto offC = QuatRotateVec(frameCorr, convV(offF));
                spineOffG = ozz::math::Float3(offC.x * unitScale, offC.y * unitScale, offC.z * unitScale);
                SAF_LOG_INFO("[FBX] SpineRide: ON spineIdx={} (parent {}) pelvisIdx={} (parent {}) off=({:.3f},{:.3f},{:.3f})",
                    spineBoneIdx, static_cast<int>(gameParents[static_cast<size_t>(spineBoneIdx)]),
                    pelvisBoneIdx, static_cast<int>(gameParents[static_cast<size_t>(pelvisBoneIdx)]),
                    spineOffG.x, spineOffG.y, spineOffG.z);
            } else {
                SAF_LOG_INFO("[FBX] SpineRide: off (spineIdx={} parent={} pelvisIdx={})",
                    spineBoneIdx,
                    spineBoneIdx >= 0 ? static_cast<int>(gameParents[static_cast<size_t>(spineBoneIdx)]) : -2,
                    pelvisBoneIdx);
            }

            // Bake the correction into the file rest worlds (per-frame file
            // roots get it below), so the retarget becomes
            //   GW = M*FW * conj(M*FB0) * GB0 — delta applied in game frame.
            for (auto& wq : fbxRestWorld)
                wq = qn(frameCorr * wq);

            // Per-bone rest realignment (replaces the old single-rotation
            // ArmCorr): the file rest (Mixamo T-pose, arms horizontal) and the
            // game bind (Starfield A-pose, arms ~45deg down) differ PER BONE.
            // The plain conjugation GW = FW*conj(FRW)*GB0 applies the file's
            // world delta on top of the game bind, ADDING that whole rest
            // difference as extra droop — the arms then swing around a
            // direction ~45deg below/behind the intended one. Rebase each
            // mapped bone's rest world by rotA = the minimal world rotation
            // from the file rest bone direction (to its first file child) to
            // the game bind direction of the matching game child; rotA is
            // identity where the rests agree (legs/spine). Then
            //   GW = FW * conj(rotA*FRW) * GB0
            // tracks the file's bone direction exactly for every pose.
            {
                int realigned = 0;
                float probe[6] = { -1.f, -1.f, -1.f, -1.f, -1.f, -1.f };
                auto probeSlot = [](const std::string& n) {
                    if (n == "LeftShoulder") return 0;
                    if (n == "LeftArm") return 1;
                    if (n == "LeftForeArm") return 2;
                    if (n == "RightShoulder") return 3;
                    if (n == "RightArm") return 4;
                    if (n == "RightForeArm") return 5;
                    return -1;
                };
                for (size_t f = 0; f < fbx_joint_names.size(); ++f) {
                    int childF = -1;
                    for (size_t c = 0; c < fbxParents.size(); ++c)
                        if (fbxParents[c] == static_cast<int16_t>(f)) { childF = static_cast<int>(c); break; }
                    if (childF < 0 || f >= fbxRestPos.size() || static_cast<size_t>(childF) >= fbxRestPos.size())
                        continue;
                    int gi = -1;
                    for (size_t i = 0; i < nJ; ++i)
                        if (gameToFbx[i] == static_cast<int>(f)) { gi = static_cast<int>(i); break; }
                    if (gi < 0) continue;
                    int childG = -1;
                    for (size_t i = 0; i < nJ; ++i)
                        if (gameToFbx[i] == childF && gameParents[i] == static_cast<int16_t>(gi)) {
                            childG = static_cast<int>(i);
                            break;
                        }
                    if (childG < 0 || static_cast<size_t>(gi) >= gameRestPos.size() ||
                        static_cast<size_t>(childG) >= gameRestPos.size())
                        continue;
                    const auto dF = norm3(sub3(fbxRestPos[static_cast<size_t>(childF)], fbxRestPos[f]));
                    const auto dG = norm3(sub3(gameRestPos[static_cast<size_t>(childG)], gameRestPos[static_cast<size_t>(gi)]));
                    if (isZero3(dF) || isZero3(dG)) continue;
                    const auto dFg = norm3(QuatRotateVec(frameCorr, dF));
                    const float dd = dFg.x * dG.x + dFg.y * dG.y + dFg.z * dG.z;
                    if (dd >= 1.f - 1e-6f) continue;
                    ozz::math::Quaternion rotA = ozz::math::Quaternion::identity();
                    if (dd <= -1.f + 1e-6f) {
                        const float ax = (dFg.x < 0.f ? -dFg.x : dFg.x);
                        ozz::math::Float3 ref =
                            (ax < 0.9f) ? ozz::math::Float3(1.f, 0.f, 0.f)
                                        : ozz::math::Float3(0.f, 1.f, 0.f);
                        const auto c = norm3(cross3(dFg, ref));
                        rotA = qn(ozz::math::Quaternion(c.x, c.y, c.z, 0.f));
                    } else {
                        const auto c = cross3(dFg, dG);
                        rotA = qn(ozz::math::Quaternion(c.x, c.y, c.z, 1.f + dd));
                    }
                    fbxRestWorld[f] = qn(rotA * fbxRestWorld[f]);
                    ++realigned;
                    const int ps = probeSlot(fbx_joint_names[f]);
                    if (ps >= 0)
                        probe[ps] = 2.f * std::acos(std::min(1.f, std::max(-1.f, rotA.w))) * 57.29578f;
                }
                SAF_LOG_INFO("[FBX] RestRealign: {}/{} file joints rebased; L(clav,arm,fore)=({:.1f},{:.1f},{:.1f}) R=({:.1f},{:.1f},{:.1f})deg trueBind={}",
                    realigned, fbx_joint_names.size(),
                    probe[0], probe[1], probe[2], probe[3], probe[4], probe[5], bindJsonApplied);
            }

            // Absolute pose copy (FBXRestAbs=1): rest := game bind makes
            // conj(restWorld)*bindWorld == identity, so GW(t) = fileWorld(t) —
            // the game bone takes the file's exact world orientation
            // (roll/twist included). C_Waist keeps the shared Hips rest and
            // is special-cased to mirror the pelvis in the sampling loop.
            if (restAbs) {
                int absN = 0;
                for (size_t i = 0; i < nJ; ++i) {
                    const int fi = gameToFbx[i];
                    if (fi < 0 || static_cast<size_t>(fi) >= fbxRestWorld.size()) continue;
                    if (static_cast<int>(i) == waistBoneIdx) continue;
                    fbxRestWorld[static_cast<size_t>(fi)] = gameRestWorld[i];
                    ++absN;
                }
                SAF_LOG_INFO("[FBX] RestAbs: {} joints rest := game bind (absolute world copy)", absN);
            }

            // Diagnostic (post-rest): constant orientation error of the
            // effective rest vs the bind: K = conj(restWorld) * bindWorld.
            // Decomposed about the bone axis into twist (roll, visible as
            // skin shear) and swing (frame-convention mismatch). swing≈0 →
            // only a roll offset remains and absolute-copy mode is safe;
            // in abs mode K should read ~0 for every mapped bone.
            {
                static const char* kDiagNames[] = {
                    "C_Hips", "C_Waist", "C_Spine", "C_Spine1", "C_Spine2", "C_Chest",
                    "C_Neck", "C_Head",
                    "L_Clavicle", "L_Biceps", "L_Forearm", "L_Wrist",
                    "R_Clavicle", "R_Biceps", "R_Forearm", "R_Wrist",
                    "L_Thigh", "L_Calf", "L_Foot", "R_Thigh", "R_Calf", "R_Foot" };
                auto qA = [](const ozz::math::Quaternion& q) {
                    return 2.f * std::acos(std::min(1.f, std::max(-1.f, std::fabs(q.w)))) * 57.29578f;
                };
                auto swingDeg = [&qn, &qA](const ozz::math::Quaternion& q, const ozz::math::Float3& a) {
                    const float t = q.x * a.x + q.y * a.y + q.z * a.z;
                    const ozz::math::Quaternion twRaw(a.x * t, a.y * t, a.z * t, q.w);
                    const float tl = std::sqrt(twRaw.x * twRaw.x + twRaw.y * twRaw.y +
                        twRaw.z * twRaw.z + twRaw.w * twRaw.w);
                    if (tl < 1e-6f)
                        return qA(q);  // twist degenerates: rotation is a pure swing
                    const ozz::math::Quaternion sw = qn(q * ozz::math::Conjugate(qn(twRaw)));
                    return 2.f * std::acos(std::min(1.f, std::max(-1.f, std::fabs(sw.w)))) * 57.29578f;
                };
                for (const char* dn : kDiagNames) {
                    const int gi = findName(skeleton->jointNames, dn);
                    if (gi < 0 || static_cast<size_t>(gi) >= nJ) continue;
                    const int fi = gameToFbx[gi];
                    if (fi < 0 || static_cast<size_t>(fi) >= fbx_joint_names.size()) continue;
                    int childF = -1;
                    for (size_t c = 0; c < fbxParents.size(); ++c)
                        if (fbxParents[c] == static_cast<int16_t>(fi)) { childF = static_cast<int>(c); break; }
                    if (childF < 0 || static_cast<size_t>(childF) >= fbxRestPos.size() ||
                        static_cast<size_t>(fi) >= fbxRestPos.size())
                        continue;
                    int childG = -1;
                    for (size_t i = 0; i < nJ; ++i)
                        if (gameToFbx[i] == childF && gameParents[i] == static_cast<int16_t>(gi)) {
                            childG = static_cast<int>(i);
                            break;
                        }
                    if (childG < 0) {
                        // Collapsed mapping (C_Waist shares Hips): accept a
                        // child whose parent maps to the same file joint.
                        for (size_t i = 0; i < nJ; ++i) {
                            const int p = gameParents[i];
                            if (p < 0 || static_cast<size_t>(p) >= nJ) continue;
                            if (gameToFbx[i] == childF && gameToFbx[p] == static_cast<int>(fi)) {
                                childG = static_cast<int>(i);
                                break;
                            }
                        }
                    }
                    if (childG < 0 || static_cast<size_t>(childG) >= gameRestPos.size()) continue;
                    const auto dF = norm3(sub3(fbxRestPos[static_cast<size_t>(childF)], fbxRestPos[static_cast<size_t>(fi)]));
                    const auto dGl = norm3(sub3(gameRestPos[static_cast<size_t>(childG)], gameRestPos[static_cast<size_t>(gi)]));
                    if (isZero3(dF) || isZero3(dGl)) continue;
                    const auto dFg = norm3(QuatRotateVec(frameCorr, dF));
                    const ozz::math::Quaternion K = ozz::math::Conjugate(fbxRestWorld[static_cast<size_t>(fi)]) *
                        gameRestWorld[static_cast<size_t>(gi)];
                    const auto dLocalGame = norm3(QuatRotateVec(
                        ozz::math::Conjugate(gameRestWorld[static_cast<size_t>(gi)]), dGl));
                    SAF_LOG_INFO("[FBX] KDiag {}: angK={:.1f} swingGame={:.1f} swingFile={:.1f} deg",
                        dn, qA(K), swingDeg(K, dLocalGame), swingDeg(K, dFg));
                }
            }
        }

        auto result = std::make_unique<RawOzzAnimation>();
        result->data = ozz::make_unique<ozz::animation::offline::RawAnimation>();
        result->data->duration = raw_anim_fbx.duration;
        result->data->tracks.resize(nJ);
        result->jointHasChannel.assign(nJ, 0);

        // Root travel (Hips XZ+bounce, converted, unit-scaled, relative to t0)
        // for actor follow (feedforward, no readback loop).
        {
            int hipsFi = -1;
            for (size_t f = 0; f < fbx_joint_names.size(); ++f)
                if (fbx_joint_names[f] == "Hips") { hipsFi = static_cast<int>(f); break; }
            if (hipsFi >= 0 && static_cast<size_t>(hipsFi) < raw_anim_fbx.tracks.size()) {
                const auto& ht = raw_anim_fbx.tracks[hipsFi];
                if (!ht.translations.empty()) {
                    const auto b0 = ht.translations.front().value;
                    for (const auto& k : ht.translations) {
                        RawOzzAnimation::RootMotionKey rk;
                        rk.time = k.time;
                        const auto cv = QuatRotateVec(frameCorr, convV(ozz::math::Float3(
                            k.value.x - b0.x, k.value.y - b0.y, k.value.z - b0.z)));
                        rk.translation = ozz::math::Float3(
                            cv.x * unitScale, cv.y * unitScale, cv.z * unitScale);
                        result->rootMotion.push_back(rk);
                    }
                    SAF_LOG_INFO("[FBX] BuildWorld: root travel keys={}", result->rootMotion.size());
                    {
                        float maxAbs = 0.f;
                        for (const auto& rk : result->rootMotion) {
                            const float m = std::sqrt(rk.translation.x * rk.translation.x +
                                rk.translation.y * rk.translation.y + rk.translation.z * rk.translation.z);
                            if (m > maxAbs) maxAbs = m;
                        }
                        const auto& first = result->rootMotion.front().translation;
                        const auto& last = result->rootMotion.back().translation;
                        SAF_LOG_INFO("[FBX] Travel range: maxAbs={:.3f}m first=({:.2f},{:.2f},{:.2f}) last=({:.2f},{:.2f},{:.2f})",
                            maxAbs, first.x, first.y, first.z, last.x, last.y, last.z);
                    }
                }
            }
            s_lastRootKeys = result->rootMotion;
            s_lastRootDur = result->data->duration;
            s_lastRootPath = std::filesystem::path(a_filePath).filename().string();
        }

        // Rotation retarget: top-down world method (axes-verified):
        //   file world   FW[f](t) = FW[parent](t) * conv(fileLocal[f](t))
        //   game world   GW[j](t) = M*FW[f](t) * conj(M*FB0[f]) * GB0[j]
        //   game local   gl[j](t) = conj(GW[parentAnim](t)) * GW[j](t)
        // M = frameCorr: rotation from the file's character frame to the
        // game's (see FrameCorr block). Roots get M premultiplied, so all
        // descendants inherit it through the parent chain.
        // The clip stores gl ABSOLUTE (NAF writer ignores rest/base).
        // Unmapped joints follow their (animated) parent rigidly.
        {
            std::vector<int> depths(nJ, -1);
            std::function<bool(int)> depthOf = [&](int j) -> bool {
                if (j < 0 || static_cast<size_t>(j) >= nJ) return true;
                if (depths[j] >= 0) return true;
                depths[j] = -2;
                const int16_t p = gameParents[j];
                int d = 0;
                if (p >= 0 && static_cast<size_t>(p) < nJ && p != static_cast<int16_t>(j)) {
                    if (depths[p] == -2) { depths[j] = 0; return true; }
                    depthOf(p);
                    d = depths[p] >= 0 ? depths[p] + 1 : 0;
                }
                depths[j] = d;
                return true;
            };
            for (size_t j = 0; j < nJ; ++j) depthOf(static_cast<int>(j));
            std::vector<int> gameOrder(nJ);
            for (size_t j = 0; j < nJ; ++j) gameOrder[j] = static_cast<int>(j);
            std::stable_sort(gameOrder.begin(), gameOrder.end(),
                [&](int a, int b) { return depths[a] < depths[b]; });

            int fbxRootIdx = -1;
            for (size_t j = 0; j < fbxParents.size(); ++j) {
                if (fbxParents[j] == -1) { fbxRootIdx = static_cast<int>(j); break; }
            }
            std::vector<float> masterTimes;
            {
                const ozz::animation::offline::RawAnimation::JointTrack* rootTr = nullptr;
                if (fbxRootIdx >= 0 && static_cast<size_t>(fbxRootIdx) < raw_anim_fbx.tracks.size())
                    rootTr = &raw_anim_fbx.tracks[fbxRootIdx];
                if (!rootTr || rootTr->rotations.empty()) {
                    for (const auto& tr : raw_anim_fbx.tracks) {
                        if (!tr.rotations.empty()) { rootTr = &tr; break; }
                    }
                }
                if (rootTr) {
                    masterTimes.reserve(rootTr->rotations.size());
                    for (const auto& k : rootTr->rotations) masterTimes.push_back(k.time);
                }
                if (masterTimes.empty())
                    masterTimes.push_back(0.0f);
            }
            auto nearestRot = [](const ozz::animation::offline::RawAnimation::JointTrack& tr, float t)
                -> ozz::math::Quaternion {
                if (tr.rotations.empty()) return ozz::math::Quaternion::identity();
                size_t lo = 0, hi = tr.rotations.size();
                while (lo < hi) {
                    size_t mid = lo + (hi - lo) / 2;
                    if (tr.rotations[mid].time < t) lo = mid + 1;
                    else hi = mid;
                }
                size_t best = lo;
                if (best >= tr.rotations.size()) best = tr.rotations.size() - 1;
                if (best > 0) {
                    const float d0 = std::abs(tr.rotations[best].time - t);
                    const float d1 = std::abs(tr.rotations[best - 1].time - t);
                    if (d1 < d0) best = best - 1;
                }
                return tr.rotations[best].value;
            };
            auto normQ = [](ozz::math::Quaternion q) {
                float len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
                if (len > 1e-8f) { float inv = 1.f / len; q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv; }
                else { q = ozz::math::Quaternion::identity(); }
                return q;
            };


            std::vector<ozz::math::Quaternion> fileWorld(fbx_joint_names.size());
            std::vector<ozz::math::Quaternion> gameWorld(nJ);
        size_t mappedBones = 0;
        for (size_t i = 0; i < nJ; ++i)
            if (gameToFbx[i] >= 0) ++mappedBones;
        {
            std::string unmapped;
            for (size_t i = 0; i < nJ; ++i)
                if (gameToFbx[i] < 0) {
                    if (!unmapped.empty()) unmapped += ",";
                    unmapped += skeleton->jointNames[i];
                }
            SAF_LOG_INFO("[FBX] Unmapped game joints ({}): {}", nJ - mappedBones, unmapped);
        }
            // Live limb trims (thighL/thighR/clavL/clavR/feet): premultiplied
            // once per chain root, descendants ride rigidly (no kinks).
            int trimIdx[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
            {
                const char* trimNames[10] = { "L_Thigh", "R_Thigh", "L_Clavicle", "R_Clavicle", "L_Foot", "R_Foot", "C_Spine", "C_Hips", "L_Calf", "R_Calf" };
                for (int k = 0; k < 10; ++k) {
                    for (size_t i = 0; i < nJ; ++i) {
                        if (skeleton->jointNames[i] == trimNames[k]) { trimIdx[k] = static_cast<int>(i); break; }
                    }
                }
            }
            float limbTrim[9][3] = {};
            GetLimbTrims(limbTrim);
            ozz::math::Quaternion trimQ[9] = {
                ozz::math::Quaternion::identity(), ozz::math::Quaternion::identity(),
                ozz::math::Quaternion::identity(), ozz::math::Quaternion::identity(),
                ozz::math::Quaternion::identity(), ozz::math::Quaternion::identity(),
                ozz::math::Quaternion::identity(), ozz::math::Quaternion::identity(),
                ozz::math::Quaternion::identity()
            };
            {
                auto makeEuler = [](float ex, float ey, float ez) {
                    const float hx = ex * 0.00872665f, hy = ey * 0.00872665f, hz = ez * 0.00872665f;
                    ozz::math::Quaternion qx(std::sin(hx), 0.f, 0.f, std::cos(hx));
                    ozz::math::Quaternion qy(0.f, std::sin(hy), 0.f, std::cos(hy));
                    ozz::math::Quaternion qz(0.f, 0.f, std::sin(hz), std::cos(hz));
                    return (qx * qy) * qz;
                };
                // parts: 0=thighL, 1=thighR, 2=clavL, 3=clavR, 4=feet, 5=spine, 6=pelvis, 7=calfL, 8=calfR
                for (int p = 0; p < 9; ++p) {
                    if (limbTrim[p][0] != 0.f || limbTrim[p][1] != 0.f || limbTrim[p][2] != 0.f)
                        trimQ[p] = makeEuler(limbTrim[p][0], limbTrim[p][1], limbTrim[p][2]);
                }
                SAF_LOG_INFO("[FBX] LimbTrims thighL=({:.0f},{:.0f},{:.0f}) thighR=({:.0f},{:.0f},{:.0f}) clavL=({:.0f},{:.0f},{:.0f}) clavR=({:.0f},{:.0f},{:.0f}) feet=({:.0f},{:.0f},{:.0f}) spine=({:.0f},{:.0f},{:.0f}) pelvis=({:.0f},{:.0f},{:.0f})",
                    limbTrim[0][0], limbTrim[0][1], limbTrim[0][2], limbTrim[1][0], limbTrim[1][1], limbTrim[1][2],
                    limbTrim[2][0], limbTrim[2][1], limbTrim[2][2], limbTrim[3][0], limbTrim[3][1], limbTrim[3][2],
                    limbTrim[4][0], limbTrim[4][1], limbTrim[4][2], limbTrim[5][0], limbTrim[5][1], limbTrim[5][2],
                    limbTrim[6][0], limbTrim[6][1], limbTrim[6][2]);
            }
            for (float t : masterTimes) {
                for (size_t f = 0; f < fbx_joint_names.size(); ++f) {
                    ozz::math::Quaternion local = fbxRestRotations[f];
                    if (f < raw_anim_fbx.tracks.size())
                        local = nearestRot(raw_anim_fbx.tracks[f], t);
                    local = convR(local);
                    const int16_t p = fbxParents[f];
                    fileWorld[f] = (p >= 0 && static_cast<size_t>(p) < fileWorld.size())
                        ? normQ(fileWorld[p] * local) : normQ(frameCorr * local);
                }
                if (yawMode == 1 && fbxHipsIdx >= 0 &&
                    static_cast<size_t>(fbxHipsIdx) < fileWorld.size())
                    fileWorld[fbxHipsIdx] = normQ(yawQ * fileWorld[fbxHipsIdx]);
                for (int gi : gameOrder) {
                    const int fbxIdx = gameToFbx[gi];
                    ozz::math::Quaternion gw;
                    if (fbxIdx >= 0 && static_cast<size_t>(fbxIdx) < fbx_joint_names.size()) {
                        gw = normQ(fileWorld[fbxIdx] *
                            ozz::math::Conjugate(fbxRestWorld[fbxIdx]) * gameRestWorld[gi]);
                        // Abs mode: the waist shares the Hips rest, so it cannot
                        // be pre-derived per joint — mirror the pelvis track.
                        if (restAbs && gi == waistBoneIdx)
                            gw = normQ(fileWorld[fbxIdx]);
                        // Waist: split the torso bend between pelvis and spine.
                        // Blend in SKIN space (T = GW*conj(bind), re-apply the
                        // waist bind): blending world orientations directly
                        // bakes a constant bow at rest — the hips' and spine's
                        // bind frames differ by ~90°.
                        if (gi == waistBoneIdx && spineBoneIdx >= 0 && pelvisBoneIdx >= 0) {
                            const int fs = gameToFbx[spineBoneIdx];
                            if (fs >= 0 && static_cast<size_t>(fs) < fileWorld.size() &&
                                static_cast<size_t>(fs) < fbxRestWorld.size()) {
                                const ozz::math::Quaternion gwSpine = normQ(fileWorld[fs] *
                                    ozz::math::Conjugate(fbxRestWorld[fs]) * gameRestWorld[spineBoneIdx]);
                                const ozz::math::Quaternion tH = normQ(gameWorld[pelvisBoneIdx] *
                                    ozz::math::Conjugate(gameRestWorld[pelvisBoneIdx]));
                                ozz::math::Quaternion tS = normQ(gwSpine *
                                    ozz::math::Conjugate(gameRestWorld[spineBoneIdx]));
                                const float d = tH.x * tS.x + tH.y * tS.y + tH.z * tS.z + tH.w * tS.w;
                                if (d < 0.f) {
                                    tS.x = -tS.x; tS.y = -tS.y; tS.z = -tS.z; tS.w = -tS.w;
                                }
                                gw = normQ(ozz::math::Quaternion(
                                    tH.x + tS.x, tH.y + tS.y, tH.z + tS.z, tH.w + tS.w) *
                                    gameRestWorld[waistBoneIdx]);
                            }
                        }
                        if (yawMode == 2 && gi == pelvisBoneIdx)
                            gw = normQ(yawQ * gw);
                    } else {
                        const int16_t p = gameParents[gi];
                        const ozz::math::Quaternion pw =
                            (p >= 0 && static_cast<size_t>(p) < nJ) ? gameWorld[p]
                                                                    : ozz::math::Quaternion::identity();
                        gw = normQ(pw * gameRestRotations[gi]);
                    }
                    gameWorld[gi] = gw;
                    const int16_t p = gameParents[gi];
                    const ozz::math::Quaternion paw =
                        (p >= 0 && static_cast<size_t>(p) < nJ) ? gameWorld[p]
                                                                : ozz::math::Quaternion::identity();
                    ozz::math::Quaternion gl = normQ(ozz::math::Conjugate(paw) * gw);
                    // Spine euler once (chain root): uprights the torso, the
                    // chain above rides rigidly (no per-joint kinks).
                    if (haveSpineE && gi >= 0 && static_cast<size_t>(gi) < skeleton->jointNames.size() &&
                        skeleton->jointNames[gi] == "C_Spine")
                        gl = normQ(spineE * gl);
                    // Constant per-chain fixes (baked once): legs yaw-180 at
                    // Hips (legs join torso, descendants ride), arms flip at
                    // clavicles (hands swap fix). Descendants ride rigidly.
                    {
                        const std::string& cgn = skeleton->jointNames[gi];
                        const int legsFix = g_fbxLegsFix.load(std::memory_order_acquire);
                        const int armsFix = g_fbxArmsFix.load(std::memory_order_acquire);
                        ozz::math::Quaternion qf(0.f, 0.f, 0.f, 1.f);
                        bool want = false;
                        if (cgn == "C_Hips" && legsFix == 1) {
                            qf = ozz::math::Quaternion(0.f, 0.f, 1.f, 0.f);
                            want = true;
                        } else if ((cgn == "L_Clavicle" || cgn == "R_Clavicle") && armsFix >= 1 && armsFix <= 3) {
                            if (armsFix == 1) qf = ozz::math::Quaternion(1.f, 0.f, 0.f, 0.f);
                            else if (armsFix == 2) qf = ozz::math::Quaternion(0.f, 1.f, 0.f, 0.f);
                            else qf = ozz::math::Quaternion(0.f, 0.f, 1.f, 0.f);
                            want = true;
                        }
                        if (want) {
                            gl = normQ(gl * qf);
                        }
                    }
                    // Live limb trims (replay-baked): premultiply once per
                    // chain root, descendants ride rigidly (no kinks).
                    {
                        int part = -1;
                        if (gi == trimIdx[0]) part = 0;
                        else if (gi == trimIdx[1]) part = 1;
                        else if (gi == trimIdx[2]) part = 2;
                        else if (gi == trimIdx[3]) part = 3;
                        else if (gi == trimIdx[4] || gi == trimIdx[5]) part = 4;
                        else if (gi == trimIdx[6]) part = 5;
                        else if (gi == trimIdx[7]) part = 6;
                        else if (gi == trimIdx[8]) part = 7;
                        else if (gi == trimIdx[9]) part = 8;
                        if (part >= 0) {
                            const float* te = limbTrim[part];
                            if (te[0] != 0.f || te[1] != 0.f || te[2] != 0.f)
                                gl = normQ(trimQ[part] * gl);
                        }
                    }
                    auto& dstKey = result->data->tracks[gi].rotations.emplace_back();
                    dstKey.time = t;
                    dstKey.value = gl;
                }
            }
            {
                size_t keyCount = result->data->tracks.empty() ? 0 : result->data->tracks[0].rotations.size();
                SAF_LOG_INFO("[FBX] World retarget: {} game joints x {} keys ({} mapped)",
                    nJ, keyCount, mappedBones);
            }
            for (size_t i = 0; i < nJ; ++i)
                if (gameToFbx[i] >= 0)
                    result->jointHasChannel[i] = 1;
        }

        // Translations: live rest offset + converted file delta (unit-scaled).
        // Pelvis travel clamped to 0.5m (file jumps/teleports panic physics).
        // Nearest-key lookup for the spine ride (pelvis track sampled at the
        // spine's key times; both tracks share the FBX bake grid).
        auto fbxNearestIdx = [](const auto& a_keys, float a_t) -> size_t {
            if (a_keys.empty()) return 0;
            size_t lo = 0, hi = a_keys.size();
            while (lo < hi) {
                const size_t mid = lo + (hi - lo) / 2;
                if (a_keys[mid].time < a_t) lo = mid + 1; else hi = mid;
            }
            size_t best = (lo < a_keys.size()) ? lo : a_keys.size() - 1;
            if (best > 0 && std::abs(a_keys[best - 1].time - a_t) < std::abs(a_keys[best].time - a_t))
                best = best - 1;
            return best;
        };
        for (size_t i = 0; i < nJ; ++i)
        {
            int fbxIdx = gameToFbx[i];
            if (fbxIdx < 0 || static_cast<size_t>(fbxIdx) >= raw_anim_fbx.tracks.size())
            {
                const auto& restT = gameRestTranslations[i];
                result->data->tracks[i].translations.push_back({ 0.0f, restT });
                result->data->tracks[i].scales.push_back({ 0.0f, ozz::math::Float3::one() });
                continue;
            }
            const auto& srcTrack = raw_anim_fbx.tracks[fbxIdx];
            const auto& restT = gameRestTranslations[i];
            const ozz::math::Float3 fbxRestTrans = fbxRestTranslations[fbxIdx];
            if (!srcTrack.translations.empty()) {
                // Pelvis stays local: bulk travel is carried by follow (actor
                // move), so it must NOT also sit in the clip — otherwise the
                // pelvis subtree gets 2x travel and separates from the torso.
                size_t tk = 0;
                for (const auto& srcKey : srcTrack.translations) {
                    auto& dstT = result->data->tracks[i].translations.emplace_back();
                    dstT.time = srcKey.time;
                    const ozz::math::Float3 deltaSrc(
                        srcKey.value.x - fbxRestTrans.x,
                        srcKey.value.y - fbxRestTrans.y,
                        srcKey.value.z - fbxRestTrans.z);
                    const ozz::math::Float3 deltaConv = QuatRotateVec(frameCorr, convV(deltaSrc));
                    ozz::math::Float3 dc(deltaConv.x * unitScale, deltaConv.y * unitScale, deltaConv.z * unitScale);
                    if (static_cast<int>(i) == pelvisBoneIdx && tk < result->rootMotion.size()) {
                        // Horizontal travel only: actor follow carries X/Y.
                        // Vertical bounce stays in the clip (dropping it here
                        // and moving the actor instead drove feet through
                        // the floor).
                        const auto& rk = result->rootMotion[tk].translation;
                        dc.x -= rk.x; dc.y -= rk.y;
                    }
                    // Spine ride (see flag before the FrameCorr block): add
                    // the pelvis drive — its clip translation delta (already
                    // free of horizontal root travel) plus the pelvis-rotation
                    // orbit of the file hips->spine offset — to the spine
                    // root's local delta.
                    if (spineRide && static_cast<int>(i) == spineBoneIdx) {
                        const auto& hipsTrans = raw_anim_fbx.tracks[static_cast<size_t>(hipsFIdx)].translations;
                        if (!hipsTrans.empty()) {
                            const size_t kp = fbxNearestIdx(hipsTrans, srcKey.time);
                            const auto& hp = hipsTrans[kp].value;
                            const auto& hpr = fbxRestTranslations[static_cast<size_t>(hipsFIdx)];
                            const ozz::math::Float3 hd(hp.x - hpr.x, hp.y - hpr.y, hp.z - hpr.z);
                            const auto hdc = QuatRotateVec(frameCorr, convV(hd));
                            ozz::math::Float3 dcP(hdc.x * unitScale, hdc.y * unitScale, hdc.z * unitScale);
                            if (kp < result->rootMotion.size()) {
                                dcP.x -= result->rootMotion[kp].translation.x;
                                dcP.y -= result->rootMotion[kp].translation.y;
                            }
                            ozz::math::Quaternion rD = ozz::math::Quaternion::identity();
                            const auto& pelvRots =
                                result->data->tracks[static_cast<size_t>(pelvisBoneIdx)].rotations;
                            if (!pelvRots.empty()) {
                                const size_t kr = fbxNearestIdx(pelvRots, srcKey.time);
                                rD = pelvRots[kr].value *
                                    ozz::math::Conjugate(gameRestRotations[static_cast<size_t>(pelvisBoneIdx)]);
                                const float rl = std::sqrt(rD.x * rD.x + rD.y * rD.y +
                                    rD.z * rD.z + rD.w * rD.w);
                                if (rl > 1e-8f) {
                                    rD.x /= rl; rD.y /= rl; rD.z /= rl; rD.w /= rl;
                                }
                            }
                            const auto rot = QuatRotateVec(rD, ozz::math::Float3(
                                spineOffG.x + dc.x, spineOffG.y + dc.y, spineOffG.z + dc.z));
                            dc.x += dcP.x + (rot.x - spineOffG.x);
                            dc.y += dcP.y + (rot.y - spineOffG.y);
                            dc.z += dcP.z + (rot.z - spineOffG.z);
                        }
                    }
                    dstT.value = ozz::math::Float3(
                        restT.x + dc.x,
                        restT.y + dc.y,
                        restT.z + dc.z);
                    ++tk;
                }
            } else {
                result->data->tracks[i].translations.push_back({ 0.0f, restT });
            }
            for (const auto& srcScale : srcTrack.scales)
            {
                auto& dstScale = result->data->tracks[i].scales.emplace_back();
                dstScale.time = srcScale.time;
                dstScale.value = srcScale.value;
            }
            if (result->data->tracks[i].scales.empty())
                result->data->tracks[i].scales.push_back({ 0.0f, ozz::math::Float3::one() });

            result->jointHasChannel[i] = 1;
        }

        // Quaternion sign continuity (adjacent keys, same hemisphere).
        {
            int fixed = 0;
            for (size_t i = 0; i < nJ; ++i) {
                auto& rots = result->data->tracks[i].rotations;
                for (size_t k = 1; k < rots.size(); ++k) {
                    const auto& p = rots[k - 1].value;
                    auto& q = rots[k].value;
                    const float dot = p.x*q.x + p.y*q.y + p.z*q.z + p.w*q.w;
                    if (dot < 0.f) {
                        q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w;
                        ++fixed;
                    }
                }
            }
            if (fixed > 0)
                SAF_LOG_INFO("[FBX] Sign continuity: flipped {} keys", fixed);
        }

        SAF_LOG_INFO("[FBX] BuildWorld: done file='{}'", a_filePath);
        // Bind pose for the delta computation: world-clip rotation keys are
        // absolute game-space locals, so rest = game local rest (NOT clip
        // frame 0, which is already a scene pose). Translations likewise.
        // World clip is already in game Z-up space: no Y-up conversion on write.
        result->needsYUpToZUpConversion = false;
        {
            result->bindPose.resize(nJ);
            for (size_t i = 0; i < nJ; ++i) {
                auto& b = result->bindPose[i];
                b.rotation = (i < gameRestRotations.size())
                    ? gameRestRotations[i]
                    : ozz::math::Quaternion::identity();
                b.translation = (i < gameRestTranslations.size())
                    ? gameRestTranslations[i]
                    : ozz::math::Float3::zero();
                b.scale = ozz::math::Float3::one();
            }
        }
        return result;
    }

    FBXAnimationFile::FBXAnimationFile(const std::string& a_filePath, std::shared_ptr<OzzSkeleton> a_gameSkeleton)
    {
        if (!a_gameSkeleton || !a_gameSkeleton->data)
            return;
        skeleton = a_gameSkeleton;
        (void)a_filePath;
    }

    std::unique_ptr<Generator> FBXAnimationFile::CreateGenerator()
    {
        if (!raw_animation || !skeleton)
        {
            return nullptr;
        }

        ozz::animation::offline::AnimationBuilder builder;
        auto runtimeAnim = builder(*raw_animation);
        if (!runtimeAnim)
        {
            return nullptr;
        }

        return std::make_unique<ClipGenerator>(skeleton, std::move(runtimeAnim));
    }

    size_t FBXAnimationFile::GetSizeBytes()
    {
        size_t total = 0;
        if (skeleton)
        {
            total += sizeof(OzzSkeleton);
        }
        if (raw_animation)
        {
            total += raw_animation->tracks.size() * sizeof(ozz::animation::offline::RawAnimation::JointTrack);
        }
        if (face_animation_data)
        {
            total += sizeof(RawOzzFaceAnimation);
        }
        return total;
    }

    float FBXAnimationFile::GetDuration()
    {
        return duration;
    }

    std::unique_ptr<IBasicAnimationContext> FBXAnimationFile::CreateContext()
    {
        return nullptr;
    }

    void FBXAnimationFile::SampleBoneAnimation(float a_time, const ozz::span<ozz::math::SoaTransform>& a_transformsOut, IBasicAnimationContext* a_context)
    {
    }

    void FBXAnimationFile::SampleFaceAnimation(float a_time, const std::span<float>& a_morphsOut)
    {
    }

    void FBXAnimationFile::SampleEventTrack(float a_time, float a_timeStep, IAnimEventHandler* a_eventHandler)
    {
    }
}
