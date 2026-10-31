#include "PGraph.h"
#include "Animation/Generator.h"
#include <algorithm>
#include <ozz/animation/runtime/skeleton.h>

namespace Animation::Procedural
{
	std::span<ozz::math::SoaTransform> PGraph::Evaluate(InstanceData& a_graphInst, PoseCache& a_poseCache)
	{
		if (actorNode >= a_graphInst.results.size()) return {};
		auto* handle = std::get_if<PoseCache::Handle>(&a_graphInst.results[actorNode]);
		return handle && handle->is_valid() ? handle->get() : std::span<ozz::math::SoaTransform>();
	}

	bool PGraph::AdvanceTime(InstanceData& a_graphInst, float a_deltaTime)
	{
		auto instIter = a_graphInst.nodeInstances.begin();
		for (auto& n : nodes) {
			n->AdvanceTime(instIter->get(), a_deltaTime);
			++instIter;
		}
		return false;
	}

	void PGraph::Synchronize(InstanceData& a_graphInst, InstanceData& a_ownerInst, PGraph* a_ownerGraph, float a_correctionDelta)
	{
		if (a_graphInst.lastSyncOwner != std::addressof(a_ownerInst)) {
			a_graphInst.syncMap.clear();
			for (auto selfIter = nodes.begin(); selfIter != nodes.end(); ++selfIter) {
				auto& n = *selfIter;
				if (n->syncId == UINT64_MAX) continue;
				for (auto ownerIter = a_ownerGraph->nodes.begin(); ownerIter != a_ownerGraph->nodes.end(); ++ownerIter) {
					auto& oN = *ownerIter;
					if (oN->syncId == n->syncId && oN->GetTypeInfo() == n->GetTypeInfo()) {
						a_graphInst.syncMap.push_back({ n.get(), a_graphInst.nodeInstances[static_cast<size_t>(std::distance(nodes.begin(), selfIter))].get(), a_ownerInst.nodeInstances[static_cast<size_t>(std::distance(a_ownerGraph->nodes.begin(), ownerIter))].get() });
						break;
					}
				}
			}
			a_graphInst.lastSyncOwner = std::addressof(a_ownerInst);
		}
		for (auto& i : a_graphInst.syncMap)
			i.node->Synchronize(i.selfInstData, i.ownerInstData, a_correctionDelta);
	}

	void PGraph::InitInstanceData(InstanceData& a_graphInst)
	{
		a_graphInst.nodeInstances.clear();
		a_graphInst.nodeInstances.reserve(nodes.size());
		for (auto& n : nodes)
			a_graphInst.nodeInstances.emplace_back(n->CreateInstanceData());
		a_graphInst.results.resize(nodes.size());
	}

	std::unique_ptr<Generator> PGraph::CreateGenerator()
	{
		return nullptr;
	}

	size_t PGraph::GetSizeBytes()
	{
		size_t result = sizeof(PGraph) + nodes.size() * sizeof(std::unique_ptr<PNode>);
		for (auto& n : nodes) result += n->GetSizeBytes();
		return result;
	}

	bool PGraph::SortNodes(std::vector<PNode*>&) { return true; }
	void PGraph::InsertCacheReleaseNodes(std::vector<PNode*>&) {}
	void PGraph::PointersToIndexes(std::vector<PNode*>&) {}
	void PGraph::EmplaceNodeOrder(std::vector<PNode*>&) {}

	bool PGraph::DepthFirstNodeSort(PNode*, size_t, std::unordered_set<PNode*>&, std::unordered_set<PNode*>&, std::vector<PNode*>&) { return true; }
}
