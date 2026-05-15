 

#include "heft.h"
#include "dag_generator.h"
#include "AlgorithmResult.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <vector>

static constexpr double HEFT_INF = std::numeric_limits<double>::max();


static double avgExecTime(const DAGData& dag, int taskId)
{
	double sum = 0.0;
	for (double t : dag.tasks[taskId].execTimes) sum += t;
	return sum / static_cast<double>(dag.tasks[taskId].execTimes.size());
}


static double commCost(const DAGData& dag, int predTask, int predVm, int succVm)
{
	if (predVm == succVm) return 0.0;
	return dag.commCostFactor * avgExecTime(dag, predTask);
}


static std::vector<double> computeUpwardRank(const DAGData& dag)
{
	int n = static_cast<int>(dag.tasks.size());
	std::vector<double> rank(n, -1.0);

	std::function<double(int)> calc = [&](int id) -> double {
		if (rank[id] >= 0.0) return rank[id];

		double best = 0.0;
		for (int succ : dag.tasks[id].successors) {
			double cc = commCost(dag, id, 0, 1);
			best = std::max(best, cc + calc(succ));
		}
		return rank[id] = avgExecTime(dag, id) + best;
	};

	for (int i = 0; i < n; ++i) calc(i);
	return rank;
}


static double computeEFT(const DAGData& dag,
						 int taskId, int vmId,
						 const std::vector<double>& vmReady,
						 const std::vector<ScheduleEntry>& scheduled,
						 const std::vector<int>& taskVm,
						 double& estOut)
{
	double est = vmReady[vmId];

	for (int predId : dag.tasks[taskId].predecessors) {
		if (taskVm[predId] < 0) continue;
		double cc = commCost(dag, predId, taskVm[predId], vmId);
		est = std::max(est, scheduled[predId].finishTime + cc);
	}

	estOut = est;
	return est + dag.tasks[taskId].execTimes[vmId];
}





AlgorithmResult heft_schedule(const DAGData& dag)
{
	auto startTime = std::chrono::high_resolution_clock::now();

	const int n = static_cast<int>(dag.tasks.size());
	const int m = static_cast<int>(dag.vms.size());

	std::vector<double> upRank = computeUpwardRank(dag);

	std::vector<int> priority(n);
	std::iota(priority.begin(), priority.end(), 0);
	std::sort(priority.begin(), priority.end(),
			  [&](int a, int b) {
				  if (upRank[a] != upRank[b]) return upRank[a] > upRank[b];
				  return a < b;
			  });

	std::vector<double>      vmReady(m, 0.0);
	std::vector<int>         taskVm(n, -1);
	std::vector<ScheduleEntry> entries(n);

	for (int tid : priority) {
		int    bestVM  = 0;
		double bestEFT = HEFT_INF;
		double bestEST = 0.0;

		for (int v = 0; v < m; ++v) {
			double est = 0.0;
			double eft = computeEFT(dag, tid, v, vmReady, entries, taskVm, est);

			bool better = (eft < bestEFT);
			if (!better && std::fabs(eft - bestEFT) < 1e-9) {
				better = (est < bestEST) ||
						 (std::fabs(est - bestEST) < 1e-9 && v < bestVM);
			}

			if (better) {
				bestVM  = v;
				bestEST = est;
				bestEFT = eft;
			}
		}

		entries[tid] = {tid, bestVM, bestEST, bestEFT};
		taskVm[tid]  = bestVM;
		vmReady[bestVM] = bestEFT;
	}

	auto endTime = std::chrono::high_resolution_clock::now();
	double runtimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

	AlgorithmResult result;
	result.algorithmName = "HEFT";
	result.algorithmDesc = "Upward-rank priority + greedy EFT VM assignment";
	result.entries       = entries;
	result.isValid        = true;
	result.makespan       = *std::max_element(vmReady.begin(), vmReady.end());
	result.runtimeMs     = runtimeMs;

	return result;
}
