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
static constexpr double COMM_FACTOR = 0.3;

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
    return COMM_FACTOR * merge_avgExec(dag, predTask);
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

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 3 — GRAPH ANALYSIS (Phase 1)
// ════════════════════════════════════════════════════════════════════════════

struct DAGProfile {
    int    numLevels;
    double avgLevelWidth;
    double maxLevelWidth;
    int    totalEdges;
    double density;          // edges / max_possible_edges
    double criticalPathLen;  // sum of avg exec times along critical path
    bool   isDeep;           // more levels than average width
    bool   isDense;          // density > 0.3
    int    parallelBranches; // count of levels with width > numVMs
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

// --- Strategy A: HEFT-style greedy for large/fast scheduling (replaces D&C) ---
static void scheduleLevel_HEFT(const DAGData& dag, const vector<int>& tasks,
                               const vector<double>& upRank, ScheduleState& st)
{
    int m = static_cast<int>(dag.vms.size());

    // Sort tasks by descending upward rank (HEFT priority)
    vector<int> sorted = tasks;
    sort(sorted.begin(), sorted.end(), [&](int a, int b) {
        if (upRank[a] != upRank[b]) return upRank[a] > upRank[b];
        return a < b;
    });

    for (int tid : sorted) {
        int    bestVM  = 0;
        double bestEFT = merge_INF;
        double bestEST = 0.0;

        for (int v = 0; v < m; ++v) {
            double est = 0.0;
            double eft = merge_computeEFT(dag, tid, v, st, est);

            bool better = (eft < bestEFT);
            if (!better && std::fabs(eft - bestEFT) < 1e-9) {
                better = (est < bestEST) || (std::fabs(est - bestEST) < 1e-9 && v < bestVM);
            }

            if (better) {
                bestVM  = v;
                bestEST = est;
                bestEFT = eft;
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

enum class LevelStrategy { HEFT, DP_LOOKAHEAD, EDP_GLOBAL };

static LevelStrategy classifyLevel(const DAGData& dag,
                                   const vector<int>& level,
                                   const vector<double>& upRank,
                                   const DAGProfile& profile)
{
    int m = static_cast<int>(dag.vms.size());
    int width = static_cast<int>(level.size());
    int totalTasks = static_cast<int>(dag.tasks.size());

    // Adaptive rules (ordered by preference):
    // 1) HEFT when tasks >> VMs (scale and speed)
    if (totalTasks >= 4 * m)
        return LevelStrategy::HEFT;

    // 2) DP+HEFT hybrid for balanced workloads (approx 2x tasks per VM)
    if (totalTasks >= 2 * m)
        return LevelStrategy::DP_LOOKAHEAD;

    // 3) EDP for crucial small/high-priority levels (critical path heavy)
    double critThreshold = profile.criticalPathLen * 0.6;
    bool hasCritical = false;
    for (int tid : level) {
        if (upRank[tid] >= critThreshold) {
            hasCritical = true;
            break;
        }
    }
    if (hasCritical && width <= 3 * m)
        return LevelStrategy::EDP_GLOBAL;

    // Default: DP look-ahead offers a good balance
    return LevelStrategy::DP_LOOKAHEAD;
}

// ════════════════════════════════════════════════════════════════════════════
//  SECTION 6 — GLOBAL REFINEMENT (Phase 3)
// ════════════════════════════════════════════════════════════════════════════

static double computeMakespan(const ScheduleState& st, int n)
{
    double ms = 0.0;
    for (int i = 0; i < n; ++i)
        if (st.taskFinish[i] > ms) ms = st.taskFinish[i];
    return ms;
}


// Full schedule replay: recomputes all EST/EFT given current taskVm assignments
// using topological order (task IDs are topologically ordered by construction)
static void replaySchedule(const DAGData& dag, ScheduleState& st)
{
    int n = static_cast<int>(dag.tasks.size());
    int m = static_cast<int>(dag.vms.size());

    st.vmReady.assign(m, 0.0);
    st.taskFinish.assign(n, 0.0);
    st.scheduled.clear();

    for (int tid = 0; tid < n; ++tid) {
        int vm = st.taskVm[tid];
        double est = 0.0;
        double eft = merge_computeEFT(dag, tid, vm, st, est);
        st.vmReady[vm]      = eft;
        st.taskFinish[tid]  = eft;
        st.scheduled[tid]   = {tid, vm, est, eft};
    }
}

// Enhanced swap-based refinement: try swapping pairs of tasks between VMs
static void tryTaskSwaps(const DAGData& dag, ScheduleState& st, int n)
{
    int m = static_cast<int>(dag.vms.size());
    double currentMakespan = computeMakespan(st, n);
    bool improved = true;

    int swapAttempts = 0;
    const int maxSwaps = min(n * n / 4, 50);  // more aggressive swap search

    while (improved && swapAttempts < maxSwaps) {
        improved = false;
        swapAttempts++;

        for (int tid1 = 0; tid1 < n - 1; ++tid1) {
            for (int tid2 = tid1 + 1; tid2 < n; ++tid2) {
                int vm1 = st.taskVm[tid1];
                int vm2 = st.taskVm[tid2];
                if (vm1 == vm2) continue;

                // Try swapping
                swap(st.taskVm[tid1], st.taskVm[tid2]);
                replaySchedule(dag, st);
                double newMakespan = computeMakespan(st, n);

                if (newMakespan < currentMakespan) {
                    currentMakespan = newMakespan;
                    improved = true;
                    break;
                } else {
                    swap(st.taskVm[tid1], st.taskVm[tid2]);
                    replaySchedule(dag, st);
                }
            }
            if (improved) break;
        }
    }
}

// Global DP-inspired optimization: try all VM combinations for top critical tasks
static void globalDPPhase(const DAGData& dag, ScheduleState& st,
                          const vector<double>& upRank, int n)
{
    int m = static_cast<int>(dag.vms.size());
    double currentMakespan = computeMakespan(st, n);
    
    // Find top critical tasks
    vector<pair<double, int>> rankedTasks;
    for (int i = 0; i < n; ++i)
        rankedTasks.push_back({upRank[i], i});
    sort(rankedTasks.rbegin(), rankedTasks.rend());
    
    // Try reassigning top critical tasks in different combinations
    int numTopTasks = min(5, n);
    vector<int> topTasks;
    for (int i = 0; i < numTopTasks; ++i)
        topTasks.push_back(rankedTasks[i].second);
    
    // Try all combinations of VM assignments for top tasks
    function<void(int, double)> tryAssignments = [&](int idx, double lastMakespan) {
        if (idx >= numTopTasks) {
            double newMakespan = computeMakespan(st, n);
            if (newMakespan < currentMakespan) {
                currentMakespan = newMakespan;
            }
            return;
        }
        
        int tid = topTasks[idx];
        int origVM = st.taskVm[tid];
        
        for (int vm = 0; vm < m; ++vm) {
            st.taskVm[tid] = vm;
            replaySchedule(dag, st);
            double testMakespan = computeMakespan(st, n);
            
            // Prune: only continue if promising
            if (testMakespan <= lastMakespan * 1.2) {
                tryAssignments(idx + 1, testMakespan);
            }
        }
        st.taskVm[tid] = origVM;
    };
    
    tryAssignments(0, currentMakespan);
    replaySchedule(dag, st);
}

// Aggressive refinement: try migrating all tasks, not just bottlenecks
static void aggressiveMigration(const DAGData& dag, ScheduleState& st, 
                                const vector<double>& upRank, int n)
{
    int m = static_cast<int>(dag.vms.size());
    double currentMakespan = computeMakespan(st, n);

    // Collect all tasks sorted by critical-path priority (upward rank)
    vector<int> allTasks(n);
    iota(allTasks.begin(), allTasks.end(), 0);
    sort(allTasks.begin(), allTasks.end(), [&](int a, int b) {
        return upRank[a] > upRank[b];  // critical-path tasks first
    });

    // Try migrating top 40% of critical tasks aggressively
    int numCritical = max(2, (n * 2) / 5);
    for (int i = 0; i < numCritical; ++i) {
        int tid = allTasks[i];
        int origVM = st.taskVm[tid];
        double bestNewMakespan = currentMakespan;
        int bestNewVM = origVM;

        for (int v = 0; v < m; ++v) {
            if (v == origVM) continue;
            st.taskVm[tid] = v;
            replaySchedule(dag, st);
            double newMakespan = computeMakespan(st, n);

            if (newMakespan < bestNewMakespan) {
                bestNewMakespan = newMakespan;
                bestNewVM = v;
            }
        }

        st.taskVm[tid] = bestNewVM;
        replaySchedule(dag, st);
        if (bestNewVM != origVM) {
            currentMakespan = bestNewMakespan;
        }
    }
}

static void globalRefinement(const DAGData& dag, ScheduleState& st,
                             const vector<double>& upRank,
                             int maxIterations = 5)
{
    int n = static_cast<int>(dag.tasks.size());
    int m = static_cast<int>(dag.vms.size());

    // ── Phase 3a: Bottleneck-targeted migration ──────────────────────────────
    for (int iter = 0; iter < maxIterations; ++iter) {
        double currentMakespan = computeMakespan(st, n);
        bool improved = false;

        int bottleneckVM = 0;
        double maxVmFinish = 0.0;
        for (int v = 0; v < m; ++v) {
            if (st.vmReady[v] > maxVmFinish) {
                maxVmFinish = st.vmReady[v];
                bottleneckVM = v;
            }
        }

        vector<int> bottleneckTasks;
        for (int tid = 0; tid < n; ++tid) {
            if (st.taskVm[tid] == bottleneckVM)
                bottleneckTasks.push_back(tid);
        }
        sort(bottleneckTasks.begin(), bottleneckTasks.end(), [&](int a, int b) {
            return st.taskFinish[a] > st.taskFinish[b];
        });

        for (int tid : bottleneckTasks) {
            int origVM = st.taskVm[tid];
            double bestNewMakespan = currentMakespan;
            int bestNewVM = origVM;

            for (int v = 0; v < m; ++v) {
                if (v == origVM) continue;
                st.taskVm[tid] = v;
                replaySchedule(dag, st);
                double newMakespan = computeMakespan(st, n);

                if (newMakespan < bestNewMakespan) {
                    bestNewMakespan = newMakespan;
                    bestNewVM = v;
                }
            }

            st.taskVm[tid] = bestNewVM;
            replaySchedule(dag, st);

            if (bestNewVM != origVM) {
                currentMakespan = bestNewMakespan;
                improved = true;
            }
        }

        if (!improved) break;
    }

    // ── Phase 3b: Aggressive critical-path migration ──────────────────────────
    aggressiveMigration(dag, st, upRank, n);

    // ── Phase 3c: Task swap optimization ─────────────────────────────────────
    tryTaskSwaps(dag, st, n);

    // ── Phase 3d: Global DP phase (EDP-inspired) ─────────────────────────────
    globalDPPhase(dag, st, upRank, n);

    // ── Phase 3e: Final aggressive pass ──────────────────────────────────────
    double bestGlobalMakespan = computeMakespan(st, n);
    ScheduleState bestState = st;

    for (int pass = 0; pass < 3; ++pass) {
        aggressiveMigration(dag, st, upRank, n);
        tryTaskSwaps(dag, st, n);
        
        double newMakespan = computeMakespan(st, n);
        if (newMakespan < bestGlobalMakespan) {
            bestGlobalMakespan = newMakespan;
            bestState = st;
        }
    }

    st = bestState;
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

    // Adaptive per-level scheduling (single pass). EDP is only used as a
    // targeted option for critical levels; we removed the aggressive global
    // EDP fallback to avoid forcing global assignments.
    ScheduleState stAdaptive = st;

    for (const auto& level : levels) {
        LevelStrategy strategy = classifyLevel(dag, level, upRank, profile);
        switch (strategy) {
            case LevelStrategy::HEFT:
                scheduleLevel_HEFT(dag, level, upRank, stAdaptive);
                break;
            case LevelStrategy::DP_LOOKAHEAD:
                scheduleLevel_DP(dag, level, biRank, stAdaptive);
                break;
            case LevelStrategy::EDP_GLOBAL:
                scheduleLevel_EDP(dag, level, biRank, stAdaptive);
                break;
        }
    }

    // Use the adaptive schedule as the starting point; do not force global EDP
    st = stAdaptive;

    // ── Phase 3: Global Refinement ───────────────────────────────────────────
    globalRefinement(dag, st, upRank, 5);

    // ── Build AlgorithmResult ────────────────────────────────────────────────
    AlgorithmResult result;
    result.algorithmName = "Optimization Algorithm (OP)";
    result.algorithmDesc = "Adaptive hybrid: HEFT + DP look-ahead + EDP global DP | Refinement pass";
    result.isValid       = true;
    result.makespan      = computeMakespan(st, n);

    for (const auto& kv : st.scheduled)
        result.entries.push_back(kv.second);

    auto t1 = chrono::high_resolution_clock::now();
    result.runtimeMs = chrono::duration<double, milli>(t1 - t0).count();

    return result;
}
