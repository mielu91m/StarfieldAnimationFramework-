#include "Node.h"

namespace Animation
{
	Node::~Node() {}

	GameNode::GameNode(RE::NiAVObject* node) : n(node)
	{
		if (node)
			localMatrix = reinterpret_cast<ozz::math::Float4x4*>(&node->local);
	}

	GameNode::~GameNode() = default;

	RealTransform GameNode::GetLocal() { return n ? RealTransform(n->local) : RealTransform(); }

	RealTransform GameNode::GetWorld() { return n ? RealTransform(n->world) : RealTransform(); }

	void GameNode::SetLocal(const RealTransform& t)
	{
		if (n) t.ToReal(n->local);
	}

	void GameNode::SetWorld(const RealTransform& t)
	{
		if (n) t.ToReal(n->world);
	}

	void GameNode::SetLocalReal(const RE::NiMatrix3& rot, const RE::NiPoint3& pos)
	{
		if (n) {
			n->local.rotate = rot;
			n->local.translate = pos;
		}
	}

	const char* GameNode::GetName() { return n && n->name.c_str() ? n->name.c_str() : ""; }

	RealTransform NullNode::GetLocal() { return RealTransform(); }
	RealTransform NullNode::GetWorld() { return RealTransform(); }
	void NullNode::SetLocal(const RealTransform&) {}
	void NullNode::SetWorld(const RealTransform&) {}
	void NullNode::SetLocalReal(const RE::NiMatrix3&, const RE::NiPoint3&) {}
	const char* NullNode::GetName() { return ""; }
}
