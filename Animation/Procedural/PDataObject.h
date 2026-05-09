#pragma once

#include "Physics/Constraint.h"
#include "Physics/Spring.h"

namespace Animation::Procedural
{
	struct PDataObject
	{
		virtual ~PDataObject() = default;
		virtual Physics::LinearConstraint* IsLinearConstraint() { return nullptr; }
		virtual Physics::AngularConstraint* IsAngularConstraint() { return nullptr; }
		virtual Physics::SpringWithBodyProperties* IsSpringProperties() { return nullptr; }
	};
}
