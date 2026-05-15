/**
 * main.cpp
 * --------
 * Entry point for the DAG Task Scheduling Generator.
 *
 * Demonstrates:
 *   1. Generating a random DAG (configurable size)
 *   2. Printing the full DAG structure to console
 *   3. Running strict validation and reporting PASS / FAIL
 *
 * Build instructions (Visual Studio Developer Command Prompt):
 *   cl /EHsc /std:c++17 main.cpp dag_generator.cpp /Fe:dag_scheduler.exe
 *
 * Or open the folder in Visual Studio and build via the IDE.
 *
 * No Qt. No external libraries. Pure Standard C++11+.
 */

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

// ============================================================
//  UTILITY – section banner
// ============================================================

static void printBanner(const std::string& title)
{
    const std::string border(60, '=');
    std::cout << "\n" << border << "\n";
    std::cout << "  " << title << "\n";
    std::cout << border << "\n";
}

// ============================================================
//  MAIN
// ============================================================

int main()
{
    // -------------------------------------------------------
    // Header
    // -------------------------------------------------------
    printBanner("DAG TASK SCHEDULING GENERATOR  |  Pure C++  |  No Qt");

    // -------------------------------------------------------
    // User Input – Number of Tasks and VMs
    // -------------------------------------------------------
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

    // Validate input
    if (numTasks <= 0) numTasks = DEFAULT_NUM_TASKS;
    if (numVMs <= 0) numVMs = DEFAULT_NUM_VMS;

    printBanner("CONFIGURATION");
    std::cout << "  Tasks : " << numTasks << "\n";
    std::cout << "  VMs   : " << numVMs   << "\n";
    std::cout << "  Edge probability : " << EDGE_PROBABILITY << "\n";

    // -------------------------------------------------------
    // Step 1 – Generate the DAG
    // -------------------------------------------------------
    printBanner("STEP 1  Generating Random DAG");
    DAGData dag = generateDAG(numTasks, numVMs);
    std::cout << "  DAG generated successfully.\n";

    // -------------------------------------------------------
    // Step 2 – Print full structure
    // -------------------------------------------------------
    printBanner("STEP 2 DAG Structure");
    printDAG(dag);

    // -------------------------------------------------------
    // Step 3 – Validate
    // -------------------------------------------------------
    printBanner("STEP 3 Validation");

    bool ok = validateDAG(dag);

    if (ok) {
        std::cout << "\n  \xE2\x9C\x94 DAG VALID  --  All structural checks passed.\n\n";
    } else {
        std::cout << "\n  \xE2\x9C\x98 DAG INVALID  --  One or more checks failed (see above).\n\n";
    }

    // -------------------------------------------------------
    // Edge statistics summary
    // -------------------------------------------------------
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
    std::cout << "  (Higher factor → communication cost is more significant)\n";
    std::cout << "\n";

    // -------------------------------------------------------
    // Step 4 – Run greedy HEFT scheduling
    // -------------------------------------------------------
  printBanner("STEP 4 HEFT Scheduling (Greedy)");
    AlgorithmResult heftResult = heft_schedule(dag);
    printAlgorithmResult(heftResult);

    // -------------------------------------------------------
    // Step 5 – Run HEFT + DP scheduling
    // -------------------------------------------------------
    printBanner("STEP LookAhead Heft Scheduling");
    AlgorithmResult dpResult = dp_heft(dag);
    printAlgorithmResult(dpResult);

    // ============================================================
//  RUN EDP_HEFT (Enhanced Dynamic Programming HEFT)
// ============================================================
    printBanner("STEP 6 – edp_heft (Enhanced DP HEFT)");
    AlgorithmResult edpResult = edp_heft(dag);
    printAlgorithmResult(edpResult);

    // Step 7 – Run Divide & Conquer scheduling
    // -------------------------------------------------------
    printBanner("STEP 7 – Divide & Conquer Scheduling");
    AlgorithmResult dacResult = dac_schedule(dag);
    printAlgorithmResult(dacResult);


    // -------------------------------------------------------
    // Step 8 – Run Merge Algorithm (Merged)
    // -------------------------------------------------------
    printBanner("STEP 8  Merge Algorithm (Merged)");
    AlgorithmResult opResult = merge_schedule(dag);
    printAlgorithmResult(opResult);

    // ============================================================
    // COMPARISON TABLE – All Algorithms Makespan & Runtime
    // ============================================================
    printBanner("ALGORITHM COMPARISON – PERFORMANCE RESULTS");

    const std::string tableBorder(100, '=');
    const std::string rowSeparator(100, '-');

    std::cout << tableBorder << "\n";
    std::cout << "  " << std::left << std::setw(35) << "Algorithm"
              << std::right << std::setw(20) << "Makespan"
              << std::right << std::setw(20) << "Runtime (ms)"
              << "  " << "\n";
    std::cout << rowSeparator << "\n";

    // Create algorithm results vector with makespan and runtime
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

    // Sort by makespan (best to worst)
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) {
                  return a.makespan < b.makespan;
              });

    std::cout << std::fixed << std::setprecision(2);

    for (size_t i = 0; i < results.size(); ++i) {
        std::string rank = (i == 0) ? " ✓ BEST" : "";
        std::cout << "  " << std::left << std::setw(35) << results[i].name
                  << std::right << std::setw(20) << results[i].makespan
                  << std::right << std::setw(20) << results[i].runtime
                  << rank << "\n";
    }

    std::cout << rowSeparator << "\n";

    // Calculate improvement and find fastest algorithm
    double bestMakespan = results[0].makespan;
    double worstMakespan = results[results.size() - 1].makespan;
    double improvement = ((worstMakespan - bestMakespan) / worstMakespan) * 100.0;

    // Find fastest runtime
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
