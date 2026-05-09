#pragma once
#include "Animation/Transform.h"
#include "Animation/Generator.h"
#include "Animation/Graph.h"
#include <REX/W32.h>

template <typename T>
struct SAFAPI_Array
{
	T* data = nullptr;
	uint64_t size = 0;
};

template <typename K, typename V>
struct SAFAPI_Map
{
	K* keys = nullptr;
	V* values = nullptr;
	uint64_t size = 0;
};

template <typename T>
struct SAFAPI_Handle
{
	T data;
	uint64_t handle = 0;
};

struct SAFAPI_AnimationIdentifer
{
	enum Type : uint8_t
	{
		kIndex = 0,
		kName = 1
	};

	Type type = kIndex;
	const char* name;
	uint64_t index = 0;
};

struct SAFAPI_TimelineData
{
	SAFAPI_Map<float, RE::NiPoint3> positions;
	SAFAPI_Map<float, RE::NiQuaternion> rotations;
};

struct SAFAPI_GraphData
{
	uint32_t flags; // Zmieniono z SFSE::stl::enumeration
	SAFAPI_Array<Animation::Node*> nodes;
	RE::NiAVObject* rootNode{};
	Animation::Transform* rootTransform;
};

typedef void (*SAFAPI_CustomGeneratorFunction)(void* a_data, Animation::Generator* a_generator, float a_deltaTime, SAFAPI_Array<Animation::Transform> a_output);
typedef void (*SAFAPI_VisitGraphFunction)(void*, SAFAPI_GraphData*);

extern "C" __declspec(dllexport) uint16_t SAFAPI_GetFeatureLevel();
extern "C" __declspec(dllexport) void SAFAPI_ReleaseHandle(uint64_t a_hndl);
extern "C" __declspec(dllexport) uint16_t SAFAPI_PlayAnimationFromGLTF(RE::Actor* a_actor, float a_transitionTime, const char* a_fileName, const SAFAPI_AnimationIdentifer& a_id);
extern "C" __declspec(dllexport) SAFAPI_Handle<SAFAPI_Array<const char*>> SAFAPI_GetSkeletonNodes(const char* a_raceEditorId);
extern "C" __declspec(dllexport) void SAFAPI_AttachClipGenerator(RE::Actor* a_actor, SAFAPI_Array<SAFAPI_TimelineData>* a_timelines, float a_transitionTime, int a_generatorType);
extern "C" __declspec(dllexport) void SAFAPI_AttachCustomGenerator(RE::Actor* a_actor, SAFAPI_CustomGeneratorFunction a_generatorFunc, SAFAPI_CustomGeneratorFunction a_onDestroyFunc, void* a_userData, float a_transitionTime);