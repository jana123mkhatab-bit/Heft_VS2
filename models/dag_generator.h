/**
 * dag_generator.h
 * ---------------
 * Header for the DAG Task Scheduling Generator.
 *
 * Defines the core data structures (Task, VM, DAGData) and
 * declares all functions for random DAG generation, printing,
 * and strict validation.
 *
 * Cloud Computing Context:
 *   In cloud workflow modeling, tasks form a Directed Acyclic Graph (DAG)
 *   where each node is a computational task and each directed edge represents
 *   a data dependency. Tasks can only begin execution after ALL predecessors
 *   have completed. VMs (Virtual Machines) execute tasks at varying speeds
 *   modeled by a 'speedFactor'. This module forms the data foundation layer
 *   compatible with scheduling algorithms such as HEFT and Divide & Conquer.
 *
 * Dependencies:
 *   Pure C++ Standard Library only. No Qt. No external libraries.
 *
 * Compatible with: Visual Studio 2019/2022, C++11 or above.
 */

#pragma once

#include <vector>
#include <string>

// ============================================================
//  CONSTANTS – adjust to change graph scale
// ============================================================

/// Default number of tasks in the generated DAG.
constexpr int DEFAULT_NUM_TASKS = 10;

/// Default number of Virtual Machines.
constexpr int DEFAULT_NUM_VMS = 5;

/// Probability [0,1] that an edge (i→j) exists (i < j).
constexpr double EDGE_PROBABILITY = 0.35;

/// Execution time range per task per VM  (seconds / arbitrary units).
constexpr double EXEC_TIME_MIN = 1.0;
constexpr double EXEC_TIME_MAX = 20.0;

/// VM speed factor range.
constexpr double SPEED_MIN = 0.5;
constexpr double SPEED_MAX = 2.0;

// ============================================================
//  DATA STRUCTURES
// ============================================================

/**
 * Task
 * ----
 * Represents a single computational unit in the DAG.
 *
 * 'predecessors'  – tasks that must finish before this task starts.
 * 'successors'    – tasks that depend on this task's output.
 * 'execTimes'     – execution time on each VM (index == VM id).
 *
 * Why adjacency list?
 *   Adjacency lists are memory-efficient for sparse graphs (typical in
 *   real-world workflow DAGs) and allow O(degree) traversal, which is
 *   ideal for scheduling algorithms that walk the graph level by level.
 */
struct Task {
    int id;                        ///< Unique zero-based task identifier.
    std::vector<int> predecessors; ///< IDs of tasks this task depends on.
    std::vector<int> successors;   ///< IDs of tasks that depend on this task.
    std::vector<double> execTimes; ///< Execution time on VM[i] (size == numVMs).
};

/**
 * VM (Virtual Machine)
 * --------------------
 * Models a heterogeneous cloud resource.
 * speedFactor > 1.0 → faster than baseline; < 1.0 → slower.
 */
struct VM {
    int id;              ///< Unique zero-based VM identifier.
    double speedFactor;  ///< Random speed multiplier in [SPEED_MIN, SPEED_MAX].
};

/**
 * DAGData
 * -------
 * Top-level container holding the complete DAG system state.
 * Passed by reference throughout the pipeline to avoid copying.
 */
struct DAGData {
    std::vector<Task> tasks; ///< All tasks (nodes of the DAG).
    std::vector<VM>   vms;   ///< All VMs (execution resources).
};

// ============================================================
//  PUBLIC API
// ============================================================

/**
 * generateDAG
 * -----------
 * Creates a random, valid DAG with 'numTasks' tasks and 'numVMs' VMs.
 *
 * Algorithm:
 *   1. Generate VMs with random speedFactors.
 *   2. Generate tasks with random execTimes per VM.
 *   3. For every pair (i, j) where i < j, add edge i→j with probability
 *      EDGE_PROBABILITY. This ordering constraint GUARANTEES acyclicity.
 *   4. Ensure at least one edge exists in the graph.
 *
 * Uses std::mt19937 seeded by std::random_device (non-deterministic).
 *
 * @param numTasks  Number of task nodes to generate.
 * @param numVMs    Number of VMs to generate.
 * @return          A fully populated DAGData structure.
 */
DAGData generateDAG(int numTasks = DEFAULT_NUM_TASKS,
                    int numVMs   = DEFAULT_NUM_VMS);

/**
 * validateDAG
 * -----------
 * Performs exhaustive structural validation of the DAG.
 *
 * Checks (all must pass for true):
 *   A. execTimes.size() == numVMs for every task.
 *   B. No self-loops (task depending on itself).
 *   C. All predecessor/successor indices are within valid range.
 *   D. Bidirectional consistency: if A→B then B lists A as predecessor
 *      and A lists B as successor.
 *   E. Acyclic property: no edge i→j where i >= j (reverse-edge check).
 *   F. At least one entry node (task with no predecessors).
 *   G. At least one exit node  (task with no successors).
 *   H. No duplicate edges in predecessor or successor lists.
 *
 * @param dag  The DAGData to validate.
 * @return     true if all checks pass, false otherwise (errors printed to cerr).
 */
bool validateDAG(const DAGData& dag);

/**
 * printDAG
 * --------
 * Prints a human-readable summary of the entire DAG to stdout.
 * Includes VM list, task adjacency details, and execution times.
 *
 * @param dag  The DAGData to print.
 */
void printDAG(const DAGData& dag);
