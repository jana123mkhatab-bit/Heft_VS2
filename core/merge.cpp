/**
 * op.cpp
 * ------
 * Implementation of merge_schedule: Optimization Algorithm (OP).
 *
 * Hybrid scheduler combining:
 *   - DP-HEFT bilateral ranking + local look-ahead
 *   - EDP-HEFT global DP state optimization
 *   - D&C level decomposition + parallel subproblem scheduling
 *
 * Three phases:
 *   Phase 1: Graph Analysis — classify DAG structure
 *   Phase 2: Adaptive Scheduling — per-level strategy selection
 *   Phase 3: Global Refinement — bottleneck detection + task migration
 */

#include "merge.h"
#include "dag_generator.h"
#include "AlgorithmResult.h"

#include <iostream>
#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>
#include <vector>
#include <map>
#include <queue>
#include <chrono>
#include <cmath>

using namespace std;

static constexpr double merge_INF = numeric_limits<double>::max() / 2.0;

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 1 — COST AND RANK HELPERS
// ════════════════════════════════════════════════════════════════════════════

static double merge_avgExec(const DAGData& dag, int taskId)
{
    const auto& et = dag.tasks[taskId].execTimes;
    double sum = 0.0;
    for (double t : et) sum += t;
    return sum / static_cast<double>(et.size());
}

static double merge_commCost(const DAGData& dag, int predTask, int predVm,
                           int succVm)
{
    if (predVm == succVm) return 0.0;
    return dag.commCostFactor * merge_avgExec(dag, predTask);
}

static vector<double> merge_upwardRank(const DAGData& dag)
{
    int n = static_cast<int>(dag.tasks.size());
    vector<double> rank(n, -1.0);

    function<double(int)> calc = [&](int id) -> double {
        if (rank[id] >= 0.0) return rank[id];
        double best = 0.0;
        for (int succ : dag.tasks[id].successors) {
            double cc = merge_commCost(dag, id, 0, 1);
            best = max(best, cc + calc(succ));
        }
        return rank[id] = merge_avgExec(dag, id) + best;
    };

    for (int i = 0; i < n; ++i) calc(i);
    return rank;
}

static vector<double> merge_downwardRank(const DAGData& dag)
{
    int n = static_cast<int>(dag.tasks.size());
    vector<double> rank(n, -1.0);

    function<double(int)> calc = [&](int id) -> double {
        if (rank[id] >= 0.0) return rank[id];
        double best = 0.0;
        for (int pred : dag.tasks[id].predecessors) {
            double cc = merge_commCost(dag, pred, 0, 1);
            best = max(best, calc(pred) + merge_avgExec(dag, pred) + cc);
        }
        return rank[id] = best;
    };

    for (int i = 0; i < n; ++i) calc(i);
    return rank;
}

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 2 — TOPOLOGICAL LEVELLING (from D&C)
// ════════════════════════════════════════════════════════════════════════════

static vector<vector<int>> merge_computeLevels(const DAGData& dag)
{
    int n = static_cast<int>(dag.tasks.size());
    vector<int> indegree(n, 0);
    for (int i = 0; i < n; ++i)
        for (int succ : dag.tasks[i].successors)
            indegree[succ]++;

    queue<int> q;
    for (int i = 0; i < n; ++i)
        if (indegree[i] == 0) q.push(i);

    vector<vector<int>> levels;
    while (!q.empty()) {
        int sz = static_cast<int>(q.size());
        vector<int> level;
        for (int i = 0; i < sz; ++i) {
            int u = q.front(); q.pop();
            level.push_back(u);
            for (int succ : dag.tasks[u].successors)
                if (--indegree[succ] == 0) q.push(succ);
        }
        levels.push_back(move(level));
    }
    return levels;
}

struct DAGProfile {
    int    numLevels;
    double avgLevelWidth;
    double maxLevelWidth;
    int    totalEdges;
    double density;
    double criticalPathLen;
    bool   isDeep;
    bool   isDense;
    int    parallelBranches;
};

static DAGProfile analyzeDAG(const DAGData& dag,
                             const vector<vector<int>>& levels,
                             const vector<double>& upRank)
{
    int n = static_cast<int>(dag.tasks.size());
    int m = static_cast<int>(dag.vms.size());
    DAGProfile p{};

    p.numLevels = static_cast<int>(levels.size());

    double totalWidth = 0.0;
    p.maxLevelWidth = 0.0;
    p.parallelBranches = 0;
    for (const auto& lv : levels) {
        double w = static_cast<double>(lv.size());
        totalWidth += w;
        p.maxLevelWidth = max(p.maxLevelWidth, w);
        if (static_cast<int>(lv.size()) > m) p.parallelBranches++;
    }
    p.avgLevelWidth = (p.numLevels > 0) ? totalWidth / p.numLevels : 1.0;

    p.totalEdges = 0;
    for (const auto& t : dag.tasks)
        p.totalEdges += static_cast<int>(t.successors.size());

    double maxEdges = static_cast<double>(n) * (n - 1) / 2.0;
    p.density = (maxEdges > 0) ? p.totalEdges / maxEdges : 0.0;

    // Critical path length = max upward rank (already computed)
    p.criticalPathLen = *max_element(upRank.begin(), upRank.end());

    p.isDeep  = (p.numLevels > p.avgLevelWidth);
    p.isDense = (p.density > 0.3);

    return p;
}

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 4 — SCHEDULING PRIMITIVES
// ════════════════════════════════════════════════════════════════════════════

// Shared schedule state used across all strategy functions
struct ScheduleState {
    vector<double>          vmReady;
    vector<double>          taskFinish;  // indexed by taskId
    vector<int>             taskVm;      // indexed by taskId
    map<int, ScheduleEntry> scheduled;
};

static double merge_computeEFT(const DAGData& dag, int taskId, int vmId,
                            const ScheduleState& st, double& estOut)
{
    double est = st.vmReady[vmId];
    for (int predId : dag.tasks[taskId].predecessors) {
        if (st.taskVm[predId] < 0) continue;
        double cc = merge_commCost(dag, predId, st.taskVm[predId], vmId);
        est = max(est, st.taskFinish[predId] + cc);
    }
    estOut = est;
    return est + dag.tasks[taskId].execTimes[vmId];
}

static void commitTask(ScheduleState& st, int taskId, int vmId,
                       double est, double eft)
{
    st.vmReady[vmId]      = eft;
    st.taskFinish[taskId] = eft;
    st.taskVm[taskId]     = vmId;
    st.scheduled[taskId]  = {taskId, vmId, est, eft};
}

// --- Strategy A: D&C-style greedy for highly parallel levels ---
static void scheduleLevel_DaC(const DAGData& dag, const vector<int>& tasks,
                              ScheduleState& st)
{
    int m = static_cast<int>(dag.vms.size());

    // Sort tasks by descending average exec time (heavy first)
    vector<int> sorted = tasks;
    sort(sorted.begin(), sorted.end(), [&](int a, int b) {
        return merge_avgExec(dag, a) > merge_avgExec(dag, b);
    });

    for (int tid : sorted) {
        int    bestVM  = 0;
        double bestEFT = merge_INF;
        double bestEST = 0.0;

        for (int v = 0; v < m; ++v) {
            double est = 0.0;
            double eft = merge_computeEFT(dag, tid, v, st, est);
            if (eft < bestEFT) {
                bestEFT = eft;
                bestVM  = v;
                bestEST = est;
            }
        }
        commitTask(st, tid, bestVM, bestEST, bestEFT);
    }
}

// --- Strategy X: Greedy Min-Min for levels with uniform or moderate tasks ---
static void scheduleLevel_MinMin(const DAGData& dag, const vector<int>& tasks,
                                 ScheduleState& st)
{
    int m = static_cast<int>(dag.vms.size());
    if (tasks.empty()) return;

    // Remaining tasks set
    vector<int> remaining = tasks;

    while (!remaining.empty()) {
        // For each task, find its best VM (min EFT)
        double bestGlobalEFT = merge_INF;
        int bestTaskIdx = 0;
        int bestTaskId = remaining[0];
        int bestTaskVM = 0;

        for (size_t ri = 0; ri < remaining.size(); ++ri) {
            int tid = remaining[ri];
            double bestEFT = merge_INF;
            int    bestVM  = 0;

            for (int v = 0; v < m; ++v) {
                double est = 0.0;
                double eft = merge_computeEFT(dag, tid, v, st, est);
                if (eft < bestEFT) {
                    bestEFT = eft;
                    bestVM  = v;
                }
            }

            if (bestEFT < bestGlobalEFT) {
                bestGlobalEFT = bestEFT;
                bestTaskIdx = static_cast<int>(ri);
                bestTaskId = tid;
                bestTaskVM = bestVM;
            }
        }

        // Commit the chosen task
        double est = 0.0;
        double eft = merge_computeEFT(dag, bestTaskId, bestTaskVM, st, est);
        commitTask(st, bestTaskId, bestTaskVM, est, eft);

        // Remove assigned task from remaining
        remaining.erase(remaining.begin() + bestTaskIdx);
    }
}

// --- Strategy Y: HEFT per-level (upward-rank priority) ---
static void scheduleLevel_HEFT(const DAGData& dag, const vector<int>& tasks,
                               const vector<double>& upRank, ScheduleState& st)
{
    int m = static_cast<int>(dag.vms.size());
    vector<int> sorted = tasks;
    sort(sorted.begin(), sorted.end(), [&](int a, int b){
        if (upRank[a] != upRank[b]) return upRank[a] > upRank[b];
        return a < b;
    });

    for (int tid : sorted) {
        int bestVM = 0;
        double bestEFT = merge_INF;
        double bestEST = 0.0;

        for (int v = 0; v < m; ++v) {
            double est = 0.0;
            double eft = merge_computeEFT(dag, tid, v, st, est);
            if (eft < bestEFT) {
                bestEFT = eft;
                bestVM = v;
                bestEST = est;
            }
        }
        commitTask(st, tid, bestVM, bestEST, bestEFT);
    }
}

// --- Strategy B: DP look-ahead for medium-width levels ---
static void scheduleLevel_DP(const DAGData& dag, const vector<int>& tasks,
                             const vector<double>& biRank, ScheduleState& st)
{
    int m = static_cast<int>(dag.vms.size());

    // Sort by bilateral rank descending within the level
    vector<int> sorted = tasks;
    sort(sorted.begin(), sorted.end(), [&](int a, int b) {
        return biRank[a] > biRank[b];
    });

    for (size_t pi = 0; pi < sorted.size(); ++pi) {
        int tid = sorted[pi];
        int    bestVM    = 0;
        double bestEFT   = merge_INF;
        double bestEST   = 0.0;
        double bestScore = merge_INF;

        for (int v = 0; v < m; ++v) {
            double est = 0.0;
            double eft = merge_computeEFT(dag, tid, v, st, est);

            // 2-task look-ahead within this level
            double lookahead = 0.0;
            if (pi + 1 < sorted.size()) {
                int nextTid = sorted[pi + 1];
                // Simulate committing current task
                ScheduleState tmpSt = st;
                commitTask(tmpSt, tid, v, est, eft);

                double bestNextEFT = merge_INF;
                for (int nv = 0; nv < m; ++nv) {
                    double nEST = 0.0;
                    double nEFT = merge_computeEFT(dag, nextTid, nv, tmpSt, nEST);
                    bestNextEFT = min(bestNextEFT, nEFT);
                }
                lookahead = 0.25 * bestNextEFT;
            }

            double score = eft + lookahead;
            if (score < bestScore) {
                bestScore = score;
                bestVM    = v;
                bestEST   = est;
                bestEFT   = eft;
            }
        }
        commitTask(st, tid, bestVM, bestEST, bestEFT);
    }
}

// --- Strategy C: EDP global DP for critical-path levels ---
static void scheduleLevel_EDP(const DAGData& dag, const vector<int>& tasks,
                              const vector<double>& biRank, ScheduleState& st)
{
    int m = static_cast<int>(dag.vms.size());
    int k = static_cast<int>(tasks.size());
    if (k == 0) return;

    // Sort by bilateral rank descending
    vector<int> sorted = tasks;
    sort(sorted.begin(), sorted.end(), [&](int a, int b) {
        return biRank[a] > biRank[b];
    });

    // DP table: dp[i][v] = best makespan when i-th task in sorted goes to VM v
    vector<vector<double>> dp(k, vector<double>(m, merge_INF));
    vector<vector<int>>    parent(k, vector<int>(m, -1));
    vector<vector<ScheduleState>> snapshots(k, vector<ScheduleState>(m, st));

    // Base case: first task on each VM
    {
        int tid = sorted[0];
        for (int v = 0; v < m; ++v) {
            double est = 0.0;
            double eft = merge_computeEFT(dag, tid, v, st, est);
            dp[0][v] = eft;
            snapshots[0][v] = st;
            commitTask(snapshots[0][v], tid, v, est, eft);
        }
    }

    // Forward fill
    for (int i = 1; i < k; ++i) {
        int tid = sorted[i];
        for (int prev_v = 0; prev_v < m; ++prev_v) {
            if (dp[i-1][prev_v] >= merge_INF) continue;
            const ScheduleState& parentSt = snapshots[i-1][prev_v];

            for (int v = 0; v < m; ++v) {
                double est = 0.0;
                double eft = merge_computeEFT(dag, tid, v, parentSt, est);
                double newMakespan = max(dp[i-1][prev_v], eft);

                if (newMakespan < dp[i][v]) {
                    dp[i][v]        = newMakespan;
                    parent[i][v]    = prev_v;
                    snapshots[i][v] = parentSt;
                    commitTask(snapshots[i][v], tid, v, est, eft);
                }
            }
        }
    }

    // Find best final VM
    int bestFinalVM = 0;
    double bestMakespan = merge_INF;
    for (int v = 0; v < m; ++v) {
        if (dp[k-1][v] < bestMakespan) {
            bestMakespan = dp[k-1][v];
            bestFinalVM  = v;
        }
    }

    // Backtrack to get assignments
    vector<int> assignment(k);
    assignment[k-1] = bestFinalVM;
    for (int i = k - 1; i > 0; --i)
        assignment[i-1] = parent[i][assignment[i]];

    // Replay assignments onto the real state
    for (int i = 0; i < k; ++i) {
        int tid = sorted[i];
        int vm  = assignment[i];
        double est = 0.0;
        double eft = merge_computeEFT(dag, tid, vm, st, est);
        commitTask(st, tid, vm, est, eft);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 5 — ADAPTIVE LEVEL CLASSIFICATION
// ════════════════════════════════════════════════════════════════════════════

enum class LevelStrategy { DAC, HEFT, GREEDY_MINMIN, DP_LOOKAHEAD, EDP_GLOBAL };

static LevelStrategy classifyLevel(const DAGData& dag,
                                   const vector<int>& level,
                                   const vector<double>& upRank,
                                   const DAGProfile& profile)
{
    int m = static_cast<int>(dag.vms.size());
    int width = static_cast<int>(level.size());
    int totalTasks = static_cast<int>(dag.tasks.size());

    // 1) Very wide levels: use D&C to avoid scheduler bottlenecks
    if (width > 2 * m)
        return LevelStrategy::DAC;

    // 2) Large workflows: HEFT for speed and scalability
    if (totalTasks >= 4 * m)
        return LevelStrategy::HEFT;

    // 3) Task uniformity: compute coefficient of variation of avg exec times
    double sum = 0.0;
    vector<double> vals;
    for (int tid : level) {
        double v = merge_avgExec(dag, tid);
        vals.push_back(v);
        sum += v;
    }
    double mean = (vals.empty() ? 0.0 : sum / static_cast<double>(vals.size()));
    double var = 0.0;
    for (double v : vals) var += (v - mean) * (v - mean);
    double stddev = (vals.empty() ? 0.0 : sqrt(var / static_cast<double>(vals.size())));
    double cov = (mean > 0.0 ? stddev / mean : 0.0);

    // If tasks are roughly uniform, prefer Min-Min greedy
    if (cov <= 0.15)
        return LevelStrategy::GREEDY_MINMIN;

    // Moderate variability: use DP look-ahead if affordable, otherwise Min-Min
    if (cov > 0.15 && cov <= 0.6) {
        if (width <= 3 * m) return LevelStrategy::DP_LOOKAHEAD;
        return LevelStrategy::GREEDY_MINMIN;
    }

    // High variability: consider critical-path and EDP if small; else DP or Min-Min
    double critThreshold = profile.criticalPathLen * 0.6;
    bool hasCritical = false;
    for (int tid : level) if (upRank[tid] >= critThreshold) { hasCritical = true; break; }

    if (hasCritical && width <= 3 * m)
        return LevelStrategy::EDP_GLOBAL;

    if (width <= 3 * m) return LevelStrategy::DP_LOOKAHEAD;

    return LevelStrategy::GREEDY_MINMIN;
}

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 6 — POSTPROCESSING REMOVED
//  The previous globalRefinement stage (task swaps, migrations, global DP)
//  has been removed to improve performance and preserve experimental
//  reproducibility. Makespan computation remains available.
// ════════════════════════════════════════════════════════════════════════════

static double computeMakespan(const ScheduleState& st, int n)
{
    double ms = 0.0;
    for (int i = 0; i < n; ++i)
        if (st.taskFinish[i] > ms) ms = st.taskFinish[i];
    return ms;
}

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 7 — PUBLIC API: merge_schedule
// ════════════════════════════════════════════════════════════════════════════

AlgorithmResult merge_schedule(const DAGData& dag)
{
    auto t0 = chrono::high_resolution_clock::now();

    int n = static_cast<int>(dag.tasks.size());
    int m = static_cast<int>(dag.vms.size());

    // ── Phase 1: Graph Analysis ──────────────────────────────────────────────
    vector<double> upRank = merge_upwardRank(dag);
    vector<double> dnRank = merge_downwardRank(dag);

    vector<double> biRank(n);
    for (int i = 0; i < n; ++i)
        biRank[i] = upRank[i] + dnRank[i];

    vector<vector<int>> levels = merge_computeLevels(dag);
    DAGProfile profile = analyzeDAG(dag, levels, upRank);

    // ── Phase 2: Adaptive Per-Level Scheduling ───────────────────────────────
    ScheduleState st;
    st.vmReady.assign(m, 0.0);
    st.taskFinish.assign(n, 0.0);
    st.taskVm.assign(n, -1);

    // Adaptive per-level scheduling: select best strategy per level
    ScheduleState stAdaptive = st;

    for (const auto& level : levels) {
        LevelStrategy strategy = classifyLevel(dag, level, upRank, profile);
        switch (strategy) {
            case LevelStrategy::DAC:
                scheduleLevel_DaC(dag, level, stAdaptive);
                break;
            case LevelStrategy::HEFT:
                scheduleLevel_HEFT(dag, level, upRank, stAdaptive);
                break;
            case LevelStrategy::GREEDY_MINMIN:
                scheduleLevel_MinMin(dag, level, stAdaptive);
                break;
            case LevelStrategy::DP_LOOKAHEAD:
                scheduleLevel_DP(dag, level, biRank, stAdaptive);
                break;
            case LevelStrategy::EDP_GLOBAL:
                scheduleLevel_EDP(dag, level, biRank, stAdaptive);
                break;
        }
    }

    // Use the adaptive schedule as the starting point; do not run a global EDP
    st = stAdaptive;

    // Phase 3 removed: no global refinement pass (performance-preserving)

    // ── Build AlgorithmResult ────────────────────────────────────────────────
    AlgorithmResult result;
    result.algorithmName = "Optimization Algorithm (OP)";
    result.algorithmDesc = "Adaptive hybrid: D&C + HEFT + Min-Min + DP + EDP";
    result.isValid       = true;
    result.makespan      = computeMakespan(st, n);

    for (const auto& kv : st.scheduled)
        result.entries.push_back(kv.second);

    auto t1 = chrono::high_resolution_clock::now();
    result.runtimeMs = chrono::duration<double, milli>(t1 - t0).count();

    return result;
}
