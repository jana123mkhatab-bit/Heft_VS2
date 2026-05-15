// Part: Includes
#include "dac.h"

#include <queue>
#include <vector>
#include <algorithm>
#include <numeric>   
#include <chrono>

using namespace std;

// Part: Helpers
static double dac_commCost(const DAGData& dag,
                           int predTask,
                           int predVm,
                           int  ,
                           int succVm)
{
    if (predVm == succVm) return 0.0;

    const auto& et = dag.tasks[predTask].execTimes;
    double avg = accumulate(et.begin(), et.end(), 0.0);
    avg /= static_cast<double>(et.size());

    return dag.commCostFactor * avg;
}

// Part: Public API Implementations
AlgorithmResult dac_schedule(const DAGData& dag) {

    auto startTimeMs = std::chrono::high_resolution_clock::now();

    int n = static_cast<int>(dag.tasks.size());
    int m = static_cast<int>(dag.vms.size());

    
    
    

    vector<int> indegree(n, 0);

    for (int i = 0; i < n; ++i)
        for (int succ : dag.tasks[i].successors)
            indegree[succ]++;

    queue<int> q;

    for (int i = 0; i < n; ++i)
        if (indegree[i] == 0)
            q.push(i);

    vector<vector<int>> levels;

    while (!q.empty()) {
        int sz = static_cast<int>(q.size());
        vector<int> level;

        for (int i = 0; i < sz; ++i) {
            int u = q.front(); q.pop();
            level.push_back(u);

            for (int succ : dag.tasks[u].successors)
                if (--indegree[succ] == 0)
                    q.push(succ);
        }

        levels.push_back(level);
    }

    
    
    

    struct LocalAssign {
        int taskId;
        int vmId;
    };

    vector<vector<LocalAssign>> clusterSchedules(levels.size());

    for (int l = 0; l < static_cast<int>(levels.size()); ++l) {

        vector<int> cluster = levels[l];

        
        sort(cluster.begin(), cluster.end(), [&](int a, int b) {

            double avgA = accumulate(
                dag.tasks[a].execTimes.begin(),
                dag.tasks[a].execTimes.end(), 0.0
            ) / m;

            double avgB = accumulate(
                dag.tasks[b].execTimes.begin(),
                dag.tasks[b].execTimes.end(), 0.0
            ) / m;

            return avgA > avgB;
        });

        vector<double> localVmFree(m, 0.0);

        for (int u : cluster) {

            int bestVM = 0;
            double bestFinish = 1e18;

            for (int v = 0; v < m; ++v) {

                double exec = dag.tasks[u].execTimes[v];
                double finish = localVmFree[v] + exec;

                if (finish < bestFinish) {
                    bestFinish = finish;
                    bestVM = v;
                }
            }

            localVmFree[bestVM] = bestFinish;
            clusterSchedules[l].push_back({u, bestVM});
        }
    }

    
    
    

    vector<double> globalVmFree(m, 0.0);
    vector<double> taskFinish(n, 0.0);
    vector<int> taskVM(n, -1);

    AlgorithmResult result;
    result.algorithmName = "Divide & Conquer";
    result.algorithmDesc = "Level-based clustering with communication-aware merging";
    result.entries.resize(n);

    for (size_t l = 0; l < clusterSchedules.size(); ++l) {

        for (const auto& sched : clusterSchedules[l]) {

            int u = sched.taskId;
            int v = sched.vmId;

            
            double readyTime = 0.0;

            for (int pred : dag.tasks[u].predecessors) {

                double commCost = 0.0;

                if (taskVM[pred] != v) {
                    commCost = dac_commCost(dag, pred, taskVM[pred], u, v);
                }

                readyTime = max(readyTime,
                    taskFinish[pred] + commCost);
            }

            double start = max(globalVmFree[v], readyTime);
            double finish = start + dag.tasks[u].execTimes[v];

            globalVmFree[v] = finish;
            taskFinish[u] = finish;
            taskVM[u] = v;

            result.entries[u] = {
                u,
                v,
                start,
                finish
            };
        }
    }

    
    
    

    double makespan = 0.0;
    for (double f : taskFinish)
        makespan = max(makespan, f);

    result.makespan = makespan;

    auto endTimeMs = std::chrono::high_resolution_clock::now();

    result.runtimeMs = std::chrono::duration<double, std::milli>(
        endTimeMs - startTimeMs
    ).count();

    result.isValid = true;

    return result;
}