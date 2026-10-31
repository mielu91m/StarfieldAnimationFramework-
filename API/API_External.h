#pragma once
#include <Windows.h> 
// ... reszta pliku
/**
 * Starfield Animation Framework (SAF) - External API
 * 
 * This header can be copied to other plugins to use SAF's functionality.
 * It provides a simple interface for playing GLTF/GLB animations on actors.
 * 
 * Usage:
 * 1. Copy this file to your project
 * 2. Include it in your source files
 * 3. Use the SAFAPI namespace functions
 * 
 * Example:
 *   SAFAPI::PlayAnimationFromGLTF(actor, 0.5f, "MyAnimation.glb", { 0 });
 */

#include <cstdint>
#include <string_view>

namespace RE
{
    class Actor;
}

namespace SAF::API
{
    /**
     * Error codes for GLTF operations
     */
    enum class GLTFErrorCode : uint32_t
    {
        kSuccess = 0,
        kFailedToLoad,
        kInvalidAnimation,
        kInvalidActor,
        kNoSkeletonData,
        kGeneratorAttachFailed,
        kUnknownError
    };

    /**
     * Animation identifier for selecting specific animations from GLTF files
     */
    struct AnimationIdentifier
    {
        uint32_t index = 0;  // Index of animation in GLTF file (0-based)
        const char* name = nullptr;  // Optional: name of animation to find
    };

    /**
     * Generator interpolation types
     */
    enum class GeneratorType : uint32_t
    {
        kLinear = 0,
        kCubic = 1,      // New in SAF 2.0
        kSlerp = 2       // New in SAF 2.0 - for rotations
    };

    /**
     * Transform data for a single bone
     */
    struct Transform
    {
        float position[3];
        float rotation[4];  // Quaternion (x, y, z, w)
        float scale[3];
    };

    /**
     * Timeline data point
     */
    struct TimelineDataPoint
    {
        float time;
        Transform transform;
    };

    /**
     * Timeline for a single bone
     */
    struct Timeline
    {
        const char* boneName;
        TimelineDataPoint* data;
        size_t dataCount;
        GeneratorType interpolationType;
    };

    /**
     * Array container for generic data
     */
    template <typename T>
    struct Array
    {
        T* data;
        size_t size;

        T& operator[](size_t index) { return data[index]; }
        const T& operator[](size_t index) const { return data[index]; }
        
        T* begin() { return data; }
        T* end() { return data + size; }
        const T* begin() const { return data; }
        const T* end() const { return data + size; }
    };

    /**
     * Handle for resource management
     */
    template <typename T>
    struct Handle
    {
        T* ptr;
        void (*deleter)(T*);

        Handle() : ptr(nullptr), deleter(nullptr) {}
        Handle(T* p, void (*d)(T*)) : ptr(p), deleter(d) {}

        ~Handle()
        {
            if (ptr && deleter) {
                deleter(ptr);
            }
        }

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        Handle(Handle&& other) noexcept
            : ptr(other.ptr), deleter(other.deleter)
        {
            other.ptr = nullptr;
            other.deleter = nullptr;
        }

        Handle& operator=(Handle&& other) noexcept
        {
            if (this != &other) {
                if (ptr && deleter) {
                    deleter(ptr);
                }
                ptr = other.ptr;
                deleter = other.deleter;
                other.ptr = nullptr;
                other.deleter = nullptr;
            }
            return *this;
        }

        T* get() const { return ptr; }
        T* operator->() const { return ptr; }
        T& operator*() const { return *ptr; }
        explicit operator bool() const { return ptr != nullptr; }
    };

    /**
     * Custom generator function signature
     * Called every frame to generate bone transforms
     * 
     * @param userData User-provided data
     * @param deltaTime Time since last frame in seconds
     * @param outputTransforms Array to fill with transforms
     * @param outputSize Size of output array
     */
    using CustomGeneratorFunction = void (*)(
        void* userData,
        float deltaTime,
        Transform* outputTransforms,
        size_t outputSize
    );

    /**
     * Graph visitor function signature
     * Called for each node in the animation graph
     * 
     * @param userData User-provided data
     * @param nodeData Node-specific data
     */
    using VisitGraphFunction = void (*)(
        void* userData,
        void* nodeData
    );

    // ============================================================================
    // API Function Type Definitions
    // ============================================================================

    /**
     * Play animation from GLTF/GLB file
     * 
     * @param a_actor Target actor
     * @param a_transitionTime Blend time in seconds
     * @param a_fileName Path to GLTF/GLB file (relative to Data/SAF/)
     * @param a_id Animation identifier
     * @return Error code
     */
    using PlayAnimationFromGLTF_Def = GLTFErrorCode (*)(
        RE::Actor* a_actor,
        float a_transitionTime,
        const char* a_fileName,
        const AnimationIdentifier& a_id
    );

    /**
     * Get skeleton nodes for a race
     * 
     * @param a_raceEditorId Editor ID of the race
     * @return Handle to array of node names (empty if no skeleton data)
     */
    using GetSkeletonNodes_Def = Handle<Array<const char*>> (*)(
        const char* a_raceEditorId
    );

    /**
     * Attach clip generator to actor
     * 
     * @param a_actor Target actor
     * @param a_timelines Array of timeline data
     * @param a_transitionTime Blend time in seconds
     * @param a_generatorType Interpolation type
     */
    using AttachClipGenerator_Def = void (*)(
        RE::Actor* a_actor,
        Array<Timeline>* a_timelines,
        float a_transitionTime,
        GeneratorType a_generatorType
    );

    /**
     * Attach custom generator to actor
     * 
     * @param a_actor Target actor
     * @param a_generatorFunc Generator function (called every frame)
     * @param a_onDestroyFunc Cleanup function (called on detach)
     * @param a_userData User data pointer
     * @param a_transitionTime Blend time in seconds
     */
    using AttachCustomGenerator_Def = void (*)(
        RE::Actor* a_actor,
        CustomGeneratorFunction a_generatorFunc,
        CustomGeneratorFunction a_onDestroyFunc,
        void* a_userData,
        float a_transitionTime
    );

    /**
     * Visit animation graph nodes
     * 
     * @param a_actor Target actor
     * @param a_visitFunc Visitor function
     * @param a_userData User data pointer
     */
    using VisitGraph_Def = void (*)(
        RE::Actor* a_actor,
        VisitGraphFunction a_visitFunc,
        void* a_userData
    );

    /**
     * Detach generator from actor
     * 
     * @param a_actor Target actor
     * @param a_transitionTime Blend time in seconds
     * @return True if successful
     */
    using DetachGenerator_Def = bool (*)(
        RE::Actor* a_actor,
        float a_transitionTime
    );

    /**
     * Synchronize animations between actors
     * 
     * @param a_actors Array of actors to synchronize
     * @param a_count Number of actors
     * @return True if successful
     */
    using SynchronizeActors_Def = bool (*)(
        RE::Actor** a_actors,
        size_t a_count
    );

    /**
     * Stop animation on actor
     * 
     * @param a_actor Target actor
     * @param a_transitionTime Blend time in seconds
     * @return True if successful
     */
    using StopAnimation_Def = bool (*)(
        RE::Actor* a_actor,
        float a_transitionTime
    );

    /**
     * Check if actor is playing SAF animation
     * 
     * @param a_actor Target actor
     * @return True if playing SAF animation
     */
    using IsPlayingAnimation_Def = bool (*)(
        RE::Actor* a_actor
    );

    /**
     * Get current animation time
     * 
     * @param a_actor Target actor
     * @return Current time in seconds, or -1 if not playing
     */
    using GetAnimationTime_Def = float (*)(
        RE::Actor* a_actor
    );

    /**
     * Set animation speed multiplier
     * 
     * @param a_actor Target actor
     * @param a_speed Speed multiplier (1.0 = normal speed)
     */
    using SetAnimationSpeed_Def = void (*)(
        RE::Actor* a_actor,
        float a_speed
    );
}

// ============================================================================
// Convenience namespace for calling API functions
// ============================================================================

namespace SAFAPI
{
    namespace detail
    {
        template <typename FuncType, typename RetType, typename... Args>
        inline RetType InvokeWithReturn(
            const std::string_view& funcName,
            RetType defaultReturn,
            Args... args)
        {
            const auto handle = GetModuleHandleA("StarfieldAnimationFramework.dll");
            if (handle != nullptr) {
                const auto addr = GetProcAddress(handle, funcName.data());
                if (addr != nullptr) {
                    return reinterpret_cast<FuncType>(addr)(args...);
                }
            }
            return defaultReturn;
        }

        template <typename FuncType, typename... Args>
        inline void Invoke(const std::string_view& funcName, Args... args)
        {
            const auto handle = GetModuleHandleA("StarfieldAnimationFramework.dll");
            if (handle != nullptr) {
                const auto addr = GetProcAddress(handle, funcName.data());
                if (addr != nullptr) {
                    reinterpret_cast<FuncType>(addr)(args...);
                }
            }
        }
    }

    /**
     * Play animation from GLTF/GLB file
     */
    inline SAF::API::GLTFErrorCode PlayAnimationFromGLTF(
        RE::Actor* actor,
        float transitionTime,
        const char* fileName,
        const SAF::API::AnimationIdentifier& id)
    {
        return detail::InvokeWithReturn<SAF::API::PlayAnimationFromGLTF_Def>(
            "SAF_PlayAnimationFromGLTF",
            SAF::API::GLTFErrorCode::kUnknownError,
            actor, transitionTime, fileName, id);
    }

    /**
     * Get skeleton nodes for a race
     */
    inline SAF::API::Handle<SAF::API::Array<const char*>> GetSkeletonNodes(
        const char* raceEditorId)
    {
        return detail::InvokeWithReturn<SAF::API::GetSkeletonNodes_Def>(
            "SAF_GetSkeletonNodes",
            SAF::API::Handle<SAF::API::Array<const char*>>(),
            raceEditorId);
    }

    /**
     * Attach clip generator
     */
    inline void AttachClipGenerator(
        RE::Actor* actor,
        SAF::API::Array<SAF::API::Timeline>* timelines,
        float transitionTime,
        SAF::API::GeneratorType generatorType)
    {
        detail::Invoke<SAF::API::AttachClipGenerator_Def>(
            "SAF_AttachClipGenerator",
            actor, timelines, transitionTime, generatorType);
    }

    /**
     * Attach custom generator
     */
    inline void AttachCustomGenerator(
        RE::Actor* actor,
        SAF::API::CustomGeneratorFunction generatorFunc,
        SAF::API::CustomGeneratorFunction onDestroyFunc,
        void* userData,
        float transitionTime)
    {
        detail::Invoke<SAF::API::AttachCustomGenerator_Def>(
            "SAF_AttachCustomGenerator",
            actor, generatorFunc, onDestroyFunc, userData, transitionTime);
    }

    /**
     * Visit animation graph
     */
    inline void VisitGraph(
        RE::Actor* actor,
        SAF::API::VisitGraphFunction visitFunc,
        void* userData)
    {
        detail::Invoke<SAF::API::VisitGraph_Def>(
            "SAF_VisitGraph",
            actor, visitFunc, userData);
    }

    /**
     * Detach generator
     */
    inline bool DetachGenerator(RE::Actor* actor, float transitionTime)
    {
        return detail::InvokeWithReturn<SAF::API::DetachGenerator_Def>(
            "SAF_DetachGenerator",
            false,
            actor, transitionTime);
    }

    /**
     * Synchronize actors
     */
    inline bool SynchronizeActors(RE::Actor** actors, size_t count)
    {
        return detail::InvokeWithReturn<SAF::API::SynchronizeActors_Def>(
            "SAF_SynchronizeActors",
            false,
            actors, count);
    }

    /**
     * Stop animation
     */
    inline bool StopAnimation(RE::Actor* actor, float transitionTime = 0.5f)
    {
        return detail::InvokeWithReturn<SAF::API::StopAnimation_Def>(
            "SAF_StopAnimation",
            false,
            actor, transitionTime);
    }

    /**
     * Check if playing animation
     */
    inline bool IsPlayingAnimation(RE::Actor* actor)
    {
        return detail::InvokeWithReturn<SAF::API::IsPlayingAnimation_Def>(
            "SAF_IsPlayingAnimation",
            false,
            actor);
    }

    /**
     * Get animation time
     */
    inline float GetAnimationTime(RE::Actor* actor)
    {
        return detail::InvokeWithReturn<SAF::API::GetAnimationTime_Def>(
            "SAF_GetAnimationTime",
            -1.0f,
            actor);
    }

    /**
     * Set animation speed
     */
    inline void SetAnimationSpeed(RE::Actor* actor, float speed)
    {
        detail::Invoke<SAF::API::SetAnimationSpeed_Def>(
            "SAF_SetAnimationSpeed",
            actor, speed);
    }
}