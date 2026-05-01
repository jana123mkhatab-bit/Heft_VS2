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
#include <functional>
#include <limits>
#include <numeric>
#include <vector>
#include <map>

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
    return sum / static_cast<double>(et.size());
}

static double edp_commCost(const DAGData& dag,int predTask, int predVm,int /*succTask*/, int succVm)
{
    if (predVm == succVm) return 0.0;
    constexpr double COMM_FACTOR = 0.3;
    return COMM_FACTOR * edp_avgExec(dag, predTask);
}

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 2 — BILATERAL RANK
// ════════════════════════════════════════════════════════════════════════════

static vector<double> edp_upwardRank(const DAGData& dag)
{
    int n = static_cast<int>(dag.tasks.size());
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

    vector<vector<double>>      dp(n, vector<double>(m, EDP_INF));
    vector<vector<int>>         parent(n, vector<int>(m, -1));

    EDPSnapshot emptySnap;
    emptySnap.vmReady    = vector<double>(m, 0.0);
    emptySnap.taskFinish = vector<double>(T, -1.0);
    emptySnap.taskVm     = vector<int>(T, -1);

    vector<vector<EDPSnapshot>> snaps(n, vector<EDPSnapshot>(m, emptySnap));

    // Base case: schedule the first task on every VM
    {
        int tid = priority[0];
        for (int v = 0; v < m; ++v) {
            double est = 0.0;
            double eft = edp_computeEFT(dag, tid, v, emptySnap, est);
            dp[0][v] = eft;
            snaps[0][v].vmReady[v]      = eft;
            snaps[0][v].taskFinish[tid] = eft;
            snaps[0][v].taskVm[tid]     = v;
        }
    }

    // Forward fill
    for (int i = 1; i < n; ++i) {
        int tid = priority[i];
        for (int prev_v = 0; prev_v < m; ++prev_v) {
            if (dp[i-1][prev_v] >= EDP_INF) continue;
            const EDPSnapshot& parentSnap = snaps[i-1][prev_v];

            for (int v = 0; v < m; ++v) {
                double est = 0.0;
                double eft = edp_computeEFT(dag, tid, v, parentSnap, est);
                double newMakespan = max(dp[i-1][prev_v], eft);

                if (newMakespan < dp[i][v]) {
                    dp[i][v]     = newMakespan;
                    parent[i][v] = prev_v;
                    snaps[i][v]              = parentSnap;
                    snaps[i][v].vmReady[v]   = eft;
                    snaps[i][v].taskFinish[tid] = eft;
                    snaps[i][v].taskVm[tid]  = v;
                }
            }
        }
    }

    // Find optimal final VM
    int    bestFinalVM  = 0;
    double bestMakespan = EDP_INF;
    for (int v = 0; v < m; ++v) {
        if (dp[n-1][v] < bestMakespan) {
            bestMakespan = dp[n-1][v];
            bestFinalVM  = v;
        }
    }

    // Backtrack
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
    int n = static_cast<int>(dag.tasks.size());
    int m = static_cast<int>(dag.vms.size());

    // Phase 1: Bilateral rank priority
    vector<double> upRank = edp_upwardRank(dag);
    vector<double> dnRank = edp_downwardRank(dag);

    vector<double> biRank(n);
    for (int i = 0; i < n; ++i)
        biRank[i] = upRank[i] + dnRank[i];

    vector<int> priority(n);
    iota(priority.begin(), priority.end(), 0);
   sort(priority.begin(), priority.end(), [&](int a, int b) {
    if (biRank[a] != biRank[b]) return biRank[a] > biRank[b];
    // Tie-break: if a is a predecessor of b, a comes first
    for (int succ : dag.tasks[a].successors)
        if (succ == b) return true;
    return a < b;   // stable fallback
});

    // Phase 2: DP VM assignment
    vector<int> assignment = edp_runDP(dag, priority);

    // Phase 3: Replay to compute exact EST/EFT timestamps
    vector<double>      vmReady(m, 0.0);
    map<int, ScheduleEntry> scheduled;

    for (int i = 0; i < n; ++i) {
        int tid = priority[i];
        int vm  = assignment[i];

        EDPSnapshot snap;
        snap.vmReady    = vmReady;
        snap.taskFinish = vector<double>(n, -1.0);
        snap.taskVm     = vector<int>(n, -1);

        for (const auto& kv : scheduled) {
            snap.taskFinish[kv.first] = kv.second.finishTime;
            snap.taskVm[kv.first]     = kv.second.vmId;
        }

        double est = 0.0;
        double eft = edp_computeEFT(dag, tid, vm, snap, est);

        scheduled[tid] = {tid, vm, est, eft};
        vmReady[vm]    = eft;
    }

    // Build AlgorithmResult
    AlgorithmResult result;
    result.algorithmName = "EDP-HEFT";
    result.algorithmDesc = "Bilateral rank + Full Global DP VM Selection  |  O(n * m^2)";
    result.isValid       = true;
    result.makespan      = *max_element(vmReady.begin(), vmReady.end());

    for (const auto& kv : scheduled)
        result.entries.push_back(kv.second);

    return result;
}