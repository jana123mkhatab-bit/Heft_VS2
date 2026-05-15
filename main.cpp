 

// Part: Includes
#include <iostream>
#include <string>
#include <iomanip>
#include <algorithm>
#include <vector>

#include "models/dag_generator.h"
#include "models/AlgorithmResult.h"
#include "core/heft.h"
#include "core/dp.h"
#include "core/Edp.h"
#include "core/dac.h"
#include "core/merge.h"
using namespace std;

// Part: Helpers
static void printBanner(const std::string& title)
{
    const std::string border(60, '=');
    std::cout << "\n" << border << "\n";
    std::cout << "  " << title << "\n";
    std::cout << border << "\n";
}

// Part: Entry Point
int main()
{
    
    
    
    printBanner("DAG TASK SCHEDULING GENERATOR  |  Pure C++  |  No Qt");

    
    
    
    int numTasks = DEFAULT_NUM_TASKS;
    int numVMs   = DEFAULT_NUM_VMS;

    std::cout << "\n  Enter number of tasks (default " << DEFAULT_NUM_TASKS << "): ";
    std::string taskInput;
    std::getline(std::cin, taskInput);
    if (!taskInput.empty()) {
        numTasks = std::stoi(taskInput);
    }

    std::cout << "  Enter number of virtual machines (default " << DEFAULT_NUM_VMS << "): ";
    std::string vmInput;
    std::getline(std::cin, vmInput);
    if (!vmInput.empty()) {
        numVMs = std::stoi(vmInput);
    }

    
    if (numTasks <= 0) numTasks = DEFAULT_NUM_TASKS;
    if (numVMs <= 0) numVMs = DEFAULT_NUM_VMS;

    printBanner("CONFIGURATION");
    std::cout << "  Tasks : " << numTasks << "\n";
    std::cout << "  VMs   : " << numVMs   << "\n";
    std::cout << "  Edge probability : " << EDGE_PROBABILITY << "\n";

    
    
    
    printBanner("STEP 1  Generating Random DAG");
    DAGData dag = generateDAG(numTasks, numVMs);
    std::cout << "  DAG generated successfully.\n";

    
    
    
    printBanner("STEP 2 DAG Structure");
    printDAG(dag);

    
    
    
    printBanner("STEP 3 Validation");

    bool ok = validateDAG(dag);

    if (ok) {
        std::cout << "\n  DAG VALID  --  All structural checks passed.\n\n";
    } else {
        std::cout << "\n  DAG INVALID  --  One or more checks failed (see above).\n\n";
    }

    
    
    
    int totalEdges = 0;
    int entryNodes = 0;
    int exitNodes  = 0;

    for (const Task& t : dag.tasks) {
        totalEdges += static_cast<int>(t.successors.size());
        if (t.predecessors.empty()) ++entryNodes;
        if (t.successors.empty())   ++exitNodes;
    }

    printBanner("GRAPH STATISTICS");
    std::cout << "  Total tasks  : " << dag.tasks.size()  << "\n";
    std::cout << "  Total VMs    : " << dag.vms.size()    << "\n";
    std::cout << "  Total edges  : " << totalEdges        << "\n";
    std::cout << "  Entry nodes  : " << entryNodes        << "\n";
    std::cout << "  Exit  nodes  : " << exitNodes         << "\n";
    std::cout << "  \n";
    std::cout << "  Communication Cost Factor (dynamically calculated): "
              << std::fixed << std::setprecision(4)
              << dag.commCostFactor << "\n";
    std::cout << "  (Higher factor communication cost is more significant)\n";
    std::cout << "\n";

    
    
    
    printBanner("STEP 4 HEFT Scheduling (Greedy)");
    AlgorithmResult heftResult = heft_schedule(dag);
    printAlgorithmResult(heftResult);

    
    
    
    
    printBanner("STEP 5 LookAhead Heft Scheduling");
    AlgorithmResult dpResult = dp_heft(dag);
    printAlgorithmResult(dpResult);

    


    printBanner("STEP 6 edp_heft (Enhanced DP HEFT)");
    AlgorithmResult edpResult = edp_heft(dag);
    printAlgorithmResult(edpResult);

    
    
    printBanner("STEP 7 Divide & Conquer Scheduling");
    AlgorithmResult dacResult = dac_schedule(dag);
    printAlgorithmResult(dacResult);


    
    
    
    printBanner("STEP 8  Merge Algorithm (Merged)");
    AlgorithmResult opResult = merge_schedule(dag);
    printAlgorithmResult(opResult);

    
    
    
    printBanner("ALGORITHM COMPARISON PERFORMANCE RESULTS");

    // Part: Comparison Table

    const std::string tableBorder(100, '=');
    const std::string rowSeparator(100, '-');

    std::cout << tableBorder << "\n";
    std::cout << "  " << std::left << std::setw(35) << "Algorithm"
              << std::right << std::setw(20) << "Makespan"
              << std::right << std::setw(20) << "Runtime (ms)"
              << "  " << "\n";
    std::cout << rowSeparator << "\n";

    
    struct AlgoResult {
        std::string name;
        double makespan;
        double runtime;
    };

    std::vector<AlgoResult> results = {
        {"HEFT (Greedy)", heftResult.makespan, heftResult.runtimeMs},
        {"LookAhead Heft", dpResult.makespan, dpResult.runtimeMs},
        {"EDP HEFT (Enhanced DP)", edpResult.makespan, edpResult.runtimeMs},
        {"Divide & Conquer (Level-based)", dacResult.makespan, dacResult.runtimeMs},
        {"Merge Algorithm", opResult.makespan, opResult.runtimeMs}
    };

    
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) {
                  return a.makespan < b.makespan;
              });

    std::cout << std::fixed << std::setprecision(2);

    for (size_t i = 0; i < results.size(); ++i) {
        std::string rank = (i == 0) ? " BEST" : "";
        std::cout << "  " << std::left << std::setw(35) << results[i].name
                  << std::right << std::setw(20) << results[i].makespan
                  << std::right << std::setw(20) << results[i].runtime
                  << rank << "\n";
    }

    std::cout << rowSeparator << "\n";

    // Part: Summary
    
    double bestMakespan = results[0].makespan;
    double worstMakespan = results[results.size() - 1].makespan;
    double improvement = ((worstMakespan - bestMakespan) / worstMakespan) * 100.0;

    
    double fastestRuntime = results[0].runtime;
    for (const auto& r : results) {
        fastestRuntime = std::min(fastestRuntime, r.runtime);
    }

    std::cout << "\n  Best makespan:           " << std::fixed << std::setprecision(2) << bestMakespan << "\n";
    std::cout << "  Improvement over worst:  " << improvement << "%\n";
    std::cout << "  Fastest execution time:  " << fastestRuntime << " ms\n\n";

    std::cout << tableBorder << "\n\n";

    return ok ? 0 : 1;
}
