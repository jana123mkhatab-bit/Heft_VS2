/**
 * dac.cpp
 * -------
 * Implementation of dac_schedule: Divide & Conquer task scheduling.
 *
 * Algorithm:
 *   STEP 1 — DIVIDE:   BFS topological levelling partitions the DAG into
 *                       independent layers. Tasks in the same layer have no
 *                       dependency on each other.
 *   STEP 2 — CONQUER:  Each layer is scheduled independently: tasks are
 *                       sorted by descending average execution time and
 *                       assigned to the locally fastest VM.
 *   STEP 3 — MERGE:    Partial schedules are combined. For each task, the
 *                       global start time respects both VM availability and
 *                       all predecessor finish times (+ communication cost
 *                       when predecessor and successor run on different VMs).
 *
 * Returns an AlgorithmResult compatible with printAlgorithmResult().
 */

#include "dac.h"
#include "dag_generator.h"
#include "AlgorithmResult.h"

#include <queue>
#include <vector>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <limits>

using namespace std;

// ── Communication cost helper ─────────────────────────────────────────────────
// Returns 0 if predVm == succVm (same VM, no transfer needed).
// Otherwise: 0.3 × average execution time of the predecessor task.
static double dac_commCost(const DAGData& dag, int predTask, int predVm, int succVm)
{
    if (predVm == succVm) return 0.0;
    constexpr double COMM_FACTOR = 0.3;
    const auto& et = dag.tasks[predTask].execTimes;
    double avg = 0.0;
    for (double t : et) avg += t;
    avg /= static_cast<double>(et.size());
    return COMM_FACTOR * avg;
}

// ═════════════════════════════════════════════════════════════════════════════
//  PUBLIC: dac_schedule
// ═════════════════════════════════════════════════════════════════════════════

AlgorithmResult dac_schedule(const DAGData& dag)
{
    auto t0 = chrono::high_resolution_clock::now();

    int n = static_cast<int>(dag.tasks.size());
    int m = static_cast<int>(dag.vms.size());

    // ── STEP 1: DIVIDE — BFS topological levels ──────────────────────────────
    vector<int> indegree(n, 0);
    for (int i = 0; i < n; ++i)
        for (int succ : dag.tasks[i].successors)
            indegree[succ]++;

    queue<int> bfsQ;
    for (int i = 0; i < n; ++i)
        if (indegree[i] == 0) bfsQ.push(i);

    vector<vector<int>> levels;
    while (!bfsQ.empty()) {
        int sz = static_cast<int>(bfsQ.size());
        vector<int> level;
        for (int i = 0; i < sz; ++i) {
            int u = bfsQ.front(); bfsQ.pop();
            level.push_back(u);
            for (int succ : dag.tasks[u].successors)
                if (--indegree[succ] == 0) bfsQ.push(succ);
        }
        levels.push_back(level);
    }

    // ── STEP 2: CONQUER — schedule each level independently ──────────────────
    // For each level: sort tasks by descending average exec time, then greedily
    // assign each task to the VM with the smallest current free time.
    struct LocalAssign { int taskId; int vmId; };
    vector<vector<LocalAssign>> clusterSchedules(levels.size());

    for (size_t l = 0; l < levels.size(); ++l) {
        vector<int> cluster = levels[l];

        // Sort by descending average execution time (heavier tasks scheduled first)
        sort(cluster.begin(), cluster.end(), [&](int a, int b) {
            double avgA = 0.0, avgB = 0.0;
            for (double t : dag.tasks[a].execTimes) avgA += t;
            for (double t : dag.tasks[b].execTimes) avgB += t;
            return avgA > avgB;
        });

        vector<double> localVmFree(m, 0.0);
        for (int u : cluster) {
            int    bestVM     = 0;
            double bestFinish = numeric_limits<double>::max();
            for (int v = 0; v < m; ++v) {
                double finish = localVmFree[v] + dag.tasks[u].execTimes[v];
                if (finish < bestFinish) { bestFinish = finish; bestVM = v; }
            }
            localVmFree[bestVM] = bestFinish;
            clusterSchedules[l].push_back({u, bestVM});
        }
    }

    // ── STEP 3: MERGE — global schedule with dependency + comm cost ───────────
    vector<double> globalVmFree(m, 0.0);
    vector<double> taskFinish(n, 0.0);
    vector<int>    taskVM(n, -1);

    AlgorithmResult result;
    result.algorithmName = "Divide & Conquer";
    result.algorithmDesc = "Level-based clustering with communication-aware merging";
    result.entries.resize(n);

    for (size_t l = 0; l < clusterSchedules.size(); ++l) {
        for (const auto& sched : clusterSchedules[l]) {
            int u = sched.taskId;
            int v = sched.vmId;

            // Ready time: latest predecessor finish + communication cost
            double readyTime = 0.0;
            for (int pred : dag.tasks[u].predecessors) {
                double cc = dac_commCost(dag, pred, taskVM[pred], v);
                readyTime = max(readyTime, taskFinish[pred] + cc);
            }

            double start  = max(globalVmFree[v], readyTime);
            double finish = start + dag.tasks[u].execTimes[v];

            globalVmFree[v] = finish;
            taskFinish[u]   = finish;
            taskVM[u]       = v;

            result.entries[u] = {u, v, start, finish};
        }
    }

    // ── Final metrics ─────────────────────────────────────────────────────────
    double makespan = 0.0;
    for (double f : taskFinish) makespan = max(makespan, f);
    result.makespan = makespan;

    auto t1 = chrono::high_resolution_clock::now();
    result.runtimeMs = chrono::duration<double, milli>(t1 - t0).count();
    result.isValid   = true;

    return result;
}
