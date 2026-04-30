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

#include "dag_generator.h"
#include "AlgorithmResult.h"
#include "dp.h"
#include "Edp.h"

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
    // Configuration – change these to scale the experiment
    // -------------------------------------------------------
    const int numTasks = DEFAULT_NUM_TASKS;  // defined in dag_generator.h
    const int numVMs   = DEFAULT_NUM_VMS;

    // -------------------------------------------------------
    // Header
    // -------------------------------------------------------
    printBanner("DAG TASK SCHEDULING GENERATOR  |  Pure C++  |  No Qt");
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
    printBanner("STEP 2 – DAG Structure");
    printDAG(dag);

    // -------------------------------------------------------
    // Step 3 – Validate
    // -------------------------------------------------------
    printBanner("STEP 3 – Validation");

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
    std::cout << "\n";

    // -------------------------------------------------------
    // Step 4 – Run HEFT + DP scheduling
    // -------------------------------------------------------
    printBanner("STEP 4 – HEFT + DP Scheduling");
    AlgorithmResult dpResult = dp_heft(dag);
    printAlgorithmResult(dpResult);

    // ============================================================
//  RUN EDP_HEFT (Enhanced Dynamic Programming HEFT)
// ============================================================
    printBanner("STEP 5 – edp_heft (Enhanced DP HEFT)");
    AlgorithmResult edpResult = edp_heft(dag);
    printAlgorithmResult(edpResult);


    return ok ? 0 : 1;
}
