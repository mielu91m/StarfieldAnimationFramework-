#pragma once

#include "RealTransform.h"
#include "PCH.h"
#include <ozz/base/maths/simd_math.h>

namespace Animation
{
	class Node
	{
	public:
		ozz::math::Float4x4* localMatrix = nullptr;

		virtual RealTransform GetLocal() = 0;
		virtual RealTransform GetWorld() = 0;
		virtual void SetLocal(const RealTransform& t) = 0;
		virtual void SetWorld(const RealTransform& t) = 0;
		virtual void SetLocalReal(const RE::NiMatrix3& rot, const RE::NiPoint3& pos) = 0;
		virtual const char* GetName() = 0;
		virtual ~Node();
	};

	class GameNode : public Node
	{
	public:
		RE::NiPointer<RE::NiAVObject> n;

		explicit GameNode(RE::NiAVObject* node);

		RealTransform GetLocal() override;
		RealTransform GetWorld() override;
		void SetLocal(const RealTransform& t) override;
		void SetWorld(const RealTransform& t) override;
		void SetLocalReal(const RE::NiMatrix3& rot, const RE::NiPoint3& pos) override;
		const char* GetName() override;
		~GameNode() override;
	};

	class NullNode : public Node
	{
	public:
		RealTransform GetLocal() override;
		RealTransform GetWorld() override;
		void SetLocal(const RealTransform&) override;
		void SetWorld(const RealTransform&) override;
		void SetLocalReal(const RE::NiMatrix3&, const RE::NiPoint3&) override;
		const char* GetName() override;
		~NullNode() override;
	};
}
