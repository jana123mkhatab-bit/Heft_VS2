 

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





 
static double commCost(const DAGData& dag, int predTask, int predVm,
                       int  , int succVm)
{
    if (predVm == succVm) return 0.0;

    const auto& et = dag.tasks[predTask].execTimes;
    double avg = 0.0;
    for (double v : et) avg += v;
    avg /= static_cast<double>(et.size());

    
    return dag.commCostFactor * avg;
}

 
static double computeEFT(const DAGData& dag,
                         int taskId, int vmId,
                         const std::vector<double>& vmReady,
                         const std::map<int, ScheduleEntry>& scheduled,
                         double& estOut)
{
    double est = vmReady[vmId];

    for (int predId : dag.tasks[taskId].predecessors) {
        auto it = scheduled.find(predId);
        if (it == scheduled.end()) continue; 

        const ScheduleEntry& se = it->second;
        double cc = commCost(dag, predId, se.vmId, taskId, vmId);
        est = std::max(est, se.finishTime + cc);
    }

    estOut = est;
    return est + dag.tasks[taskId].execTimes[vmId];
}

 
static vector<double> computeUpwardRank(const DAGData& dag)
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
        for (int succ : dag.tasks[id].successors) {
            
            double cc = commCost(dag, id, 0, succ, 1);
            best = std::max(best, cc + calc(succ));
        }
        return rank[id] = avgExec(id) + best;
    };

    for (int i = 0; i < n; ++i) calc(i);
    return rank;
}

 
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





AlgorithmResult dp_heft(const DAGData& dag)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    int n = static_cast<int>(dag.tasks.size());
    int m = static_cast<int>(dag.vms.size());

    
    vector<double> upRank = computeUpwardRank(dag);
    vector<double> dnRank = computeDownwardRank(dag);

    vector<double> biRank(n);
    for (int i = 0; i < n; ++i)
        biRank[i] = upRank[i] + dnRank[i];

    
    vector<int> priority(n);
    iota(priority.begin(), priority.end(), 0);
    sort(priority.begin(), priority.end(),
              [&](int a, int b) {
                  if (abs(biRank[a] - biRank[b]) > 1e-9) return biRank[a] > biRank[b];
                  if (abs(upRank[a] - upRank[b]) > 1e-9) return upRank[a] > upRank[b];
                  return a < b;
              });

    
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

            
            double lookahead = 0.0;
            if (pi + 1 < n) {
                int nextTid = priority[pi + 1];

                
                double oldVmReady = vmReady[v];
                vmReady[v] = eft;
                scheduled[tid] = {tid, v, est, eft};

                double bestNextEFT = INF;
                for (int nv = 0; nv < m; ++nv) {
                    double nEST = 0.0;
                    double nEFT = computeEFT(dag, nextTid, nv, vmReady, scheduled, nEST);
                    bestNextEFT = std::min(bestNextEFT, nEFT);
                }
                lookahead = 0.25 * bestNextEFT; 

                
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

        
        scheduled[tid]   = {tid, bestVM, bestEST, bestEFT};
        vmReady[bestVM]  = bestEFT;
    }

    
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





void printAlgorithmResult(const AlgorithmResult& result)
{
    const std::string border(60, '=');
    std::cout << "\n" << border << "\n";
    std::cout << "  " << result.algorithmName << "\n";
    std::cout << border << "\n\n";

    
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
