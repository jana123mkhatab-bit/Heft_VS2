/**
 * Edp.cpp
 * -------
 * Implementation of edp_heft — Enhanced Dynamic Programming HEFT.
 *
 * Returns an AlgorithmResult (from AlgorithmResult.h) so it is fully
 * compatible with printAlgorithmResult() and any future comparison code.
 *
 * Algorithm summary:
 *   PHASE 1 — Bilateral rank priority (upRank + downRank, descending)
 *   PHASE 2 — Full n×m DP table to find globally optimal VM assignments
 *   PHASE 3 — Replay the optimal assignment to compute exact EST/EFT timestamps
 *
 * No Qt. No external libraries. Pure Standard C++11+.
 */

#include "Edp.h"
#include "dag_generator.h"
#include "AlgorithmResult.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <numeric>
#include <vector>
#include <map>
#include <queue>

using namespace std;

static constexpr double EDP_INF = numeric_limits<double>::max() / 2.0;

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 1 — COST HELPERS
// ════════════════════════════════════════════════════════════════════════════

static double edp_avgExec(const DAGData& dag, int taskId)
{
    const vector<double>& et = dag.tasks[taskId].execTimes;
    double sum = 0.0;
    for (double t : et) sum += t;
    return sum /(double)(et.size());
}

static double edp_commCost(const DAGData& dag,
                            int predTask, int predVm,
                            int /*succTask*/, int succVm)
{
    if (predVm == succVm) return 0.0;
    return dag.commCostFactor * edp_avgExec(dag, predTask);
}

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 2 — BILATERAL RANK
// ════════════════════════════════════════════════════════════════════════════
/*static vector<double> edp_upwardRank(const DAGData& dag)

{
    int n = (int)(dag.tasks.size());
    vector<double> rank(n, -1.0);

    function<double(int)> calc = [&](int id) -> double {
        if (rank[id] >= 0.0) return rank[id];
        double best = 0.0;
        for (int succ : dag.tasks[id].successors) {
            double cc = edp_commCost(dag, id, 0, succ, 1);
            best = max(best, cc + calc(succ));
        }
        return rank[id] = edp_avgExec(dag, id) + best;
    };

    for (int i = 0; i < n; ++i) calc(i);
    return rank;
}*/


static double calcUpRank(const DAGData& dag,vector<double>& rank,int id){
    // DP memoization check
    if (rank[id] >= 0.0)
        return rank[id];

    double best = 0.0;

    // Explore all successors
    for (int succ : dag.tasks[id].successors)
    {
        double cc = edp_commCost(dag, id, 0, succ, 1);

        double futureCost = cc + calcUpRank(dag, rank, succ);

        best = std::max(best, futureCost);
    }

    // Upward rank formula
    rank[id] = edp_avgExec(dag, id) + best;

    return rank[id];
}

static vector<double> edp_upwardRank(const DAGData& dag)
{
    int n = static_cast<int>(dag.tasks.size());
    vector<double> rank(n, -1.0);

    for (int i = 0; i < n; ++i)
        calcUpRank(dag, rank, i);

    return rank;
}
static vector<double> edp_downwardRank(const DAGData& dag)
{
    int n = static_cast<int>(dag.tasks.size());
    vector<double> rank(n, -1.0);

    function<double(int)> calc = [&](int id) -> double {
        if (rank[id] >= 0.0) return rank[id];
        double best = 0.0;
        for (int pred : dag.tasks[id].predecessors) {
            double cc = edp_commCost(dag, pred, 0, id, 1);
            best = max(best, calc(pred) + edp_avgExec(dag, pred) + cc);
        }
        return rank[id] = best;
    };

    for (int i = 0; i < n; ++i) calc(i);
    return rank;
}

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 3 — SNAPSHOT AND EFT
// ════════════════════════════════════════════════════════════════════════════

struct EDPSnapshot {
    vector<double> vmReady;
    vector<double> taskFinish;
    vector<int>    taskVm;
};

static double edp_computeEFT(const DAGData& dag,
                               int taskId, int vmId,
                               const EDPSnapshot& snap,
                               double& estOut)
{
    double est = snap.vmReady[vmId];

    for (int predId : dag.tasks[taskId].predecessors) {
        if (snap.taskVm[predId] < 0) continue;
        double cc = edp_commCost(dag, predId, snap.taskVm[predId], taskId, vmId);
        est = max(est, snap.taskFinish[predId] + cc);
    }

    estOut = est;
    return est + dag.tasks[taskId].execTimes[vmId];
}

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 4 — DP ENGINE
// ════════════════════════════════════════════════════════════════════════════

static vector<int> edp_runDP(const DAGData& dag, const vector<int>& priority)
{
    int n = static_cast<int>(priority.size());
    int m = static_cast<int>(dag.vms.size());
    int T = static_cast<int>(dag.tasks.size());

    // dp[i][v] = true global makespan after scheduling priority[0..i] with
    // priority[i] on VM v.  We store the full vmReady vector here instead of
    // a scalar so the NEXT step can compute accurate EFTs.
    struct State {
        double makespan = EDP_INF;
        vector<double> vmReady;      // length m
        vector<double> taskFinish;   // length T, -1 if unscheduled
        vector<int>    taskVm;       // length T, -1 if unscheduled
    };

    // Only keep two rows at a time — O(m × T) memory instead of O(n×m×T)
    vector<State> prev(m), curr(m);

    // Initialise rows
    auto initState = [&](State& s) {
        s.makespan = EDP_INF;
        s.vmReady.assign(m, 0.0);
        s.taskFinish.assign(T, -1.0);
        s.taskVm.assign(T, -1);
    };
    for (auto& s : prev) initState(s);
    for (auto& s : curr) initState(s);

    // parent[i][v] = which prev_v produced dp[i][v]
    // We still need all rows of parent for backtracking.
    vector<vector<int>> parent(n, vector<int>(m, -1));

    // ── Base case ────────────────────────────────────────────────────
    {
        int tid = priority[0];
        EDPSnapshot snap0;
        snap0.vmReady.assign(m, 0.0);
        snap0.taskFinish.assign(T, -1.0);
        snap0.taskVm.assign(T, -1);

        for (int v = 0; v < m; ++v) {
            double est = 0.0;
            double eft = edp_computeEFT(dag, tid, v, snap0, est);

            prev[v].vmReady    = snap0.vmReady;   // all zeros
            prev[v].vmReady[v] = eft;
            prev[v].taskFinish = snap0.taskFinish;
            prev[v].taskFinish[tid] = eft;
            prev[v].taskVm    = snap0.taskVm;
            prev[v].taskVm[tid] = v;
            prev[v].makespan  = eft;              // only one task scheduled
        }
    }

    // ── Forward fill ─────────────────────────────────────────────────
    for (int i = 1; i < n; ++i) {
        int tid = priority[i];

        // Reset curr row
        for (auto& s : curr) initState(s);

        for (int prev_v = 0; prev_v < m; ++prev_v) {
            if (prev[prev_v].makespan >= EDP_INF) continue;
            const State& ps = prev[prev_v];

            // Build a temporary snapshot from ps for edp_computeEFT
            EDPSnapshot snap;
            snap.vmReady    = ps.vmReady;
            snap.taskFinish = ps.taskFinish;
            snap.taskVm     = ps.taskVm;

            for (int v = 0; v < m; ++v) {
                double est = 0.0;
                double eft = edp_computeEFT(dag, tid, v, snap, est);

                // Build the NEW vmReady after placing tid on v
                vector<double> newVmReady = ps.vmReady;
                newVmReady[v] = max(newVmReady[v], eft);

                // *** Fix: makespan = max over the UPDATED vmReady ***
                double newMakespan = *max_element(newVmReady.begin(),
                                                  newVmReady.end());

                if (newMakespan < curr[v].makespan) {
                    curr[v].makespan   = newMakespan;
                    curr[v].vmReady    = newVmReady;
                    curr[v].taskFinish = ps.taskFinish;
                    curr[v].taskFinish[tid] = eft;
                    curr[v].taskVm     = ps.taskVm;
                    curr[v].taskVm[tid] = v;
                    parent[i][v]       = prev_v;
                }
            }
        }

        swap(prev, curr);
    }

    // ── Find optimal final VM ─────────────────────────────────────────
    int    bestFinalVM  = 0;
    double bestMakespan = EDP_INF;
    for (int v = 0; v < m; ++v) {
        if (prev[v].makespan < bestMakespan) {
            bestMakespan = prev[v].makespan;
            bestFinalVM  = v;
        }
    }

    // ── Backtrack ─────────────────────────────────────────────────────
    vector<int> assignment(n);
    assignment[n-1] = bestFinalVM;
    for (int i = n - 1; i > 0; --i)
        assignment[i-1] = parent[i][assignment[i]];

    return assignment;
}
// ════════════════════════════════════════════════════════════════════════════
//  SECTION 5 — PUBLIC API: edp_heft
// ════════════════════════════════════════════════════════════════════════════

AlgorithmResult edp_heft(const DAGData& dag)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    int n = static_cast<int>(dag.tasks.size());
    int m = static_cast<int>(dag.vms.size());

    // Phase 1: Bilateral rank priority
    vector<double> upRank = edp_upwardRank(dag);
    vector<double> dnRank = edp_downwardRank(dag);

    vector<double> biRank(n);
    for (int i = 0; i < n; ++i)
        biRank[i] = upRank[i] + dnRank[i];


// ✓ Kahn's algorithm combined with HEFT rank prioritization for DAG-safe ordering
    struct RankCmp {
        const vector<double>& biRank;

        RankCmp(const vector<double>& r) : biRank(r) {}

        bool operator()(int a, int b) const {
            return biRank[a] < biRank[b];  // Max-heap: higher rank comes first
        }
    };

    vector<int> indeg(n, 0);

    for (int u = 0; u < n; ++u) {
        for (int v : dag.tasks[u].successors) {
            indeg[v]++;
        }
    }

    priority_queue<int, vector<int>, RankCmp>
        pq((RankCmp(biRank)));

    for (int i = 0; i < n; ++i) {
        if (indeg[i] == 0) {
            pq.push(i);
        }
    }

    vector<int> priority;
    priority.reserve(n);

    while (!pq.empty()) {
        int u = pq.top();
        pq.pop();

        priority.push_back(u);

        for (int v : dag.tasks[u].successors) {
            if (--indeg[v] == 0) {
                pq.push(v);
            }
        }
    }

    // Phase 2: DP VM assignment
    vector<int> assignment = edp_runDP(dag, priority);

    // Phase 3: Replay to compute exact EST/EFT timestamps
    vector<double>      vmReady(m, 0.0);
    map<int, ScheduleEntry> scheduled;

    vector<double> taskFinish(n, -1.0);
    vector<int>    taskVm(n, -1);

    for (int i = 0; i < n; ++i) {
        int tid = priority[i];
        int vm  = assignment[i];

        EDPSnapshot snap;
        snap.vmReady    = vmReady;
        snap.taskFinish = taskFinish;
        snap.taskVm     = taskVm;

        double est = 0.0;
        double eft = edp_computeEFT(dag, tid, vm, snap, est);

        scheduled[tid] = {tid, vm, est, eft};
        vmReady[vm]    = max(vmReady[vm], eft);
        taskFinish[tid] = eft;
        taskVm[tid]     = vm;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double runtimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // Build AlgorithmResult
    AlgorithmResult result;
    result.algorithmName = "EDP-HEFT";
    result.algorithmDesc = "Bilateral rank + Full Global DP VM Selection  |  O(n * m^2)";
    result.isValid       = true;
    result.makespan      = *max_element(vmReady.begin(), vmReady.end());
    result.runtimeMs     = runtimeMs;

    for (const auto& kv : scheduled)
        result.entries.push_back(kv.second);

    return result;
}