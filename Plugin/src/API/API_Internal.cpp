#include "PCH.h"
#include "API_Internal.h"
#include "Animation/GraphManager.h"
#include "Animation/Generator.h"
#include "Animation/Ozz.h"
#include "Util/OzzUtil.h"

namespace
{
	class SAFAPI_UserGenerator : public Animation::Generator
	{
	public:
		std::vector<ozz::math::SoaTransform> output_buffer;
		const ozz::animation::Skeleton* skeleton = nullptr;
		void* userData = nullptr;
		std::vector<Animation::Transform> userOutput;
		SAFAPI_CustomGeneratorFunction generateFunc = nullptr;
		SAFAPI_CustomGeneratorFunction onDestroyFunc = nullptr;
		bool detaching = false;

		// Teraz override pasuje do definicji w Generator.h
		virtual std::span<ozz::math::SoaTransform> Generate(Animation::IAnimEventHandler* a_eventHandler) override
		{
			if (detaching || !generateFunc) [[unlikely]] {
				return output_buffer;
			}

			if (userOutput.empty() && skeleton) {
				userOutput.resize(skeleton->num_joints());
			}

			SAFAPI_Array<Animation::Transform> api_out{ userOutput.data(), userOutput.size() };
			
			// Wywołanie zewnętrznej funkcji API
			generateFunc(userData, this, 0.016f, api_out); 

			// Konwersja wyników użytkownika na format Ozz SOA
			Util::Ozz::PackSoaTransforms(userOutput, output_buffer);

			return output_buffer;
		}

		virtual ~SAFAPI_UserGenerator() {
			if (onDestroyFunc) onDestroyFunc(userData, this, 0.0f, {nullptr, 0});
		}
	};
}

extern "C" uint16_t SAFAPI_GetFeatureLevel() { return 1; }

extern "C" uint16_t SAFAPI_PlayAnimationFromGLTF(RE::Actor* a_actor, float a_transitionTime, const char* a_fileName, const SAFAPI_AnimationIdentifer& a_id)
{
	if (!a_actor || !a_fileName) return 0;
	Animation::GraphManager::GetSingleton()->LoadAndStartAnimation(a_actor, std::string_view(a_fileName));
	return 1;
}

void SAFAPI_AttachCustomGenerator(RE::Actor* a_actor, SAFAPI_CustomGeneratorFunction a_generatorFunc, SAFAPI_CustomGeneratorFunction a_onDestroyFunc, void* a_userData, float a_transitionTime)
{
	if (!a_actor || !a_generatorFunc) return;

	auto gen = std::make_unique<SAFAPI_UserGenerator>();
	gen->generateFunc = a_generatorFunc;
	gen->onDestroyFunc = a_onDestroyFunc;
	gen->userData = a_userData;

	Animation::GraphManager::GetSingleton()->AttachGenerator(a_actor, std::move(gen), a_transitionTime);
}