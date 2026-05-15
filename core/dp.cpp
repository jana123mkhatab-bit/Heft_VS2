/**
 * dp.cpp
 * ------
 * Implementation of dp_heft: HEFT enhanced with Dynamic Programming look-ahead.
 *
 * Key differences from standard HEFT:
 *   1. Task priority = upRank + downRank  (bilateral rank)
 *      This gives tasks in the middle of the critical path a higher combined
 *      priority than pure upRank would, leading to different orderings.
 *
 *   2. VM assignment uses a 2-task look-ahead DP:
 *      For each candidate VM for task T[i], we simulate the assignment and
 *      then project the best possible EFT for the NEXT task T[i+1].
 *      The score = EFT(T[i]) + 0.25 * bestEFT(T[i+1])
 *      The VM with the lowest score wins. This avoids locally-greedy choices
 *      that would block the next task on a slow VM.
 *
 * Data structures used are the pure C++ structs from dag_generator.h
 * (Task, VM, DAGData) — no Qt, no external libraries.
 */

#include "dp.h"
#include "dag_generator.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <numeric>
#include <map>
#include <vector>
#include <cmath>
using namespace std;

static constexpr double INF = numeric_limits<double>::max();

// ============================================================
//  INTERNAL HELPERS
// ============================================================

/**
 * commCost
 * --------
 * Communication cost between a predecessor task and a successor task
 * when they run on DIFFERENT VMs. Returns 0 if same VM.
 *
 * Cost model: average execution time of the predecessor task,
 * scaled by the dynamically calculated communication factor.
 */
static double commCost(const DAGData& dag, int predTask, int predVm,
                       int /*succTask*/, int succVm)
{
    if (predVm == succVm) return 0.0;

    const auto& et = dag.tasks[predTask].execTimes;
    double avg = 0.0;
    for (double v : et) avg += v;
    avg /= static_cast<double>(et.size());

    // Use dynamically calculated communication cost factor
    return dag.commCostFactor * avg;
}

/**
 * computeEFT
 * ----------
 * Earliest Finish Time of 'taskId' on 'vmId', given:
 *   - vmReady[vmId]   : when that VM becomes free.
 *   - scheduled       : already-scheduled tasks (taskId → ScheduleEntry).
 *
 * EST = max(vmReady[vmId], max over all predecessors of (finishTime + commCost))
 * EFT = EST + execTimes[vmId]
 *
 * Sets estOut to the computed EST (for recording in the schedule).
 */
static double computeEFT(const DAGData& dag,
                         int taskId, int vmId,
                         const std::vector<double>& vmReady,
                         const std::map<int, ScheduleEntry>& scheduled,
                         double& estOut)
{
    double est = vmReady[vmId];

    for (int predId : dag.tasks[taskId].predecessors) {
        auto it = scheduled.find(predId);
        if (it == scheduled.end()) continue; // not yet scheduled

        const ScheduleEntry& se = it->second;
        double cc = commCost(dag, predId, se.vmId, taskId, vmId);
        est = std::max(est, se.finishTime + cc);
    }

    estOut = est;
    return est + dag.tasks[taskId].execTimes[vmId];
}

/**
 * computeUpwardRank
 * -----------------
 * rank_u(t) = avgExecTime(t) + max over successors s of (commCost(t,s) + rank_u(s))
 * Computed recursively with memoisation.
 */
static vector<double> computeUpwardRank(const DAGData& dag)
{
    int n = static_cast<int>(dag.tasks.size());
    std::vector<double> rank(n, -1.0);

    // Average execution time helper
    auto avgExec = [&](int id) {
        double s = 0.0;
        for (double v : dag.tasks[id].execTimes) s += v;
        return s / static_cast<double>(dag.tasks[id].execTimes.size());
    };

    std::function<double(int)> calc = [&](int id) -> double {
        if (rank[id] >= 0.0) return rank[id];

        double best = 0.0;
        for (int succ : dag.tasks[id].successors) {
            // Use VM 0 and VM 1 as representative pair for commCost
            double cc = commCost(dag, id, 0, succ, 1);
            best = std::max(best, cc + calc(succ));
        }
        return rank[id] = avgExec(id) + best;
    };

    for (int i = 0; i < n; ++i) calc(i);
    return rank;
}

/**
 * computeDownwardRank
 * -------------------
 * rank_d(t) = max over predecessors p of (rank_d(p) + avgExecTime(p) + commCost(p,t))
 * Entry tasks (no predecessors) have rank_d = 0.
 * Computed recursively with memoisation.
 */
static vector<double> computeDownwardRank(const DAGData& dag)
{
    int n = static_cast<int>(dag.tasks.size());
    std::vector<double> rank(n, -1.0);

    auto avgExec = [&](int id) {
        double s = 0.0;
        for (double v : dag.tasks[id].execTimes) s += v;
        return s / static_cast<double>(dag.tasks[id].execTimes.size());
    };

    std::function<double(int)> calc = [&](int id) -> double {
        if (rank[id] >= 0.0) return rank[id];

        double best = 0.0;
        for (int pred : dag.tasks[id].predecessors) {
            double cc = commCost(dag, pred, 0, id, 1);
            best = std::max(best, calc(pred) + avgExec(pred) + cc);
        }
        return rank[id] = best;
    };

    for (int i = 0; i < n; ++i) calc(i);
    return rank;
}

// ============================================================
//  PUBLIC: dp_heft
// ============================================================

AlgorithmResult dp_heft(const DAGData& dag)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    int n = static_cast<int>(dag.tasks.size());
    int m = static_cast<int>(dag.vms.size());

    // ── Step 1: Compute bilateral rank ──────────────────────────────────────
    vector<double> upRank = computeUpwardRank(dag);
    vector<double> dnRank = computeDownwardRank(dag);

    vector<double> biRank(n);
    for (int i = 0; i < n; ++i)
        biRank[i] = upRank[i] + dnRank[i];

    // ── Step 2: Sort tasks by bilateral rank (descending) ────────────────────
    vector<int> priority(n);
    iota(priority.begin(), priority.end(), 0);
    sort(priority.begin(), priority.end(),
              [&](int a, int b) {
                  if (abs(biRank[a] - biRank[b]) > 1e-9) return biRank[a] > biRank[b];
                  if (abs(upRank[a] - upRank[b]) > 1e-9) return upRank[a] > upRank[b];
                  return a < b;
              });

    // ── Step 3: Greedy assignment with 2-task look-ahead DP ─────────────────
    vector<double> vmReady(m, 0.0);
    map<int, ScheduleEntry> scheduled;

    for (int pi = 0; pi < n; ++pi) {
        int tid = priority[pi];

        int bestVM = 0;
        double bestEFT   = INF;
        double bestEST   = 0.0;
        double bestScore = INF;

        for (int v = 0; v < m; ++v) {
            double est = 0.0;
            double eft = computeEFT(dag, tid, v, vmReady, scheduled, est);

            // ── 2-task look-ahead: project best EFT for the next task ────────
            double lookahead = 0.0;
            if (pi + 1 < n) {
                int nextTid = priority[pi + 1];

                // Temporarily commit current task to VM v in-place
                double oldVmReady = vmReady[v];
                vmReady[v] = eft;
                scheduled[tid] = {tid, v, est, eft};

                double bestNextEFT = INF;
                for (int nv = 0; nv < m; ++nv) {
                    double nEST = 0.0;
                    double nEFT = computeEFT(dag, nextTid, nv, vmReady, scheduled, nEST);
                    bestNextEFT = std::min(bestNextEFT, nEFT);
                }
                lookahead = 0.25 * bestNextEFT; // 25% weight on next task

                // Undo temporary commit
                vmReady[v] = oldVmReady;
                scheduled.erase(tid);
            }

            double score = eft + lookahead;
            if (score < bestScore) {
                bestScore = score;
                bestVM    = v;
                bestEST   = est;
                bestEFT   = eft;
            }
        }

        // Commit the chosen assignment
        scheduled[tid]   = {tid, bestVM, bestEST, bestEFT};
        vmReady[bestVM]  = bestEFT;
    }

    // ── Step 4: Build result ─────────────────────────────────────────────────
    auto endTime = std::chrono::high_resolution_clock::now();
    double runtimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    AlgorithmResult result;
    result.algorithmName = "HEFT + DP";
    result.algorithmDesc = "Bilateral rank with 2-task look-ahead";
    result.isValid       = true;
    for (const auto& kv : scheduled)
        result.entries.push_back(kv.second);

    result.makespan = *std::max_element(vmReady.begin(), vmReady.end());
    result.runtimeMs = runtimeMs;
    return result;
}

// ============================================================
//  PUBLIC: printAlgorithmResult (for DP results)
// ============================================================

void printAlgorithmResult(const AlgorithmResult& result)
{
    const std::string border(60, '=');
    std::cout << "\n" << border << "\n";
    std::cout << "  " << result.algorithmName << "\n";
    std::cout << border << "\n\n";

    // Sort entries by task ID for clean display
    std::vector<ScheduleEntry> sorted = result.entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const ScheduleEntry& a, const ScheduleEntry& b){
                  return a.taskId < b.taskId;
              });

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  " << std::setw(8) << "Task"
              << std::setw(8) << "VM"
              << std::setw(12) << "Start"
              << std::setw(12) << "Finish" << "\n";
    std::cout << "  " << std::string(40, '-') << "\n";

    for (const ScheduleEntry& se : sorted) {
        std::cout << "  "
                  << std::setw(8) << se.taskId
                  << std::setw(8) << se.vmId
                  << std::setw(12) << se.startTime
                  << std::setw(12) << se.finishTime << "\n";
    }

    std::cout << "\n  Makespan : " << result.makespan << "\n";
    std::cout << border << "\n\n";
}
