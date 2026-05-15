/**
 * dag_generator.cpp
 * -----------------
 * Implementation of the DAG Task Scheduling Generator.
 *
 * Implements:
 *   - generateDAG()  : random DAG creation using mt19937
 *   - calculateCommCostFactor()  : dynamic comm cost factor based on DAG characteristics
 *   - validateDAG()  : strict structural validation (8 checks)
 *   - printDAG()     : formatted console output
 *
 * Design Notes:
 *   Acyclicity is guaranteed by construction: edges are only ever
 *   created from node i to node j where j > i. This means no topological
 *   sort is needed during generation – the node index IS the topological order.
 *   This approach matches standard research methodology for synthetic DAG
 *   benchmarks (e.g., GE, STG, Epigenomics workflows).
 *
 *   Communication cost factor is now dynamically calculated based on:
 *     - Task granularity (fine-grained tasks → lower factor)
 *     - System heterogeneity (more diverse VMs → higher factor)
 *     - Graph density (denser graphs → higher factor)
 *
 * Compatibility: Visual Studio 2019/2022, C++11 or above.
 *   No Qt. No external libraries. Pure Standard C++.
 */

#include "dag_generator.h"

#include <iostream>   // std::cout, std::cerr
#include <iomanip>    // std::fixed, std::setprecision, std::setw
#include <random>     // std::random_device, std::mt19937, distributions
#include <set>        // std::set (duplicate-edge detection)
#include <algorithm>  // std::find
#include <cmath>      // std::sqrt
#include <limits>     // std::numeric_limits

// ============================================================
//  INTERNAL HELPERS (file-scope only, not exposed in header)
// ============================================================

/**
 * makeRandomEngine
 * ----------------
 * Creates and returns a seeded Mersenne Twister engine.
 * std::random_device provides non-deterministic entropy on most platforms.
 */
static std::mt19937 makeRandomEngine()
{
    // Fixed seed → same DAG every run (reproducible results).
    // Change the number below to get a different-but-consistent DAG.
    constexpr unsigned int FIXED_SEED = 42u;
    return std::mt19937(FIXED_SEED);
}

/**
 * generateVMs
 * -----------
 * Populates 'vms' with 'numVMs' VMs, each assigned a random speedFactor
 * drawn from a uniform distribution in [SPEED_MIN, SPEED_MAX].
 */
static void generateVMs(std::vector<VM>& vms, int numVMs, std::mt19937& gen)
{
    std::uniform_real_distribution<double> speedDist(SPEED_MIN, SPEED_MAX);

    vms.clear();
    vms.reserve(numVMs);

    for (int v = 0; v < numVMs; ++v) {
        VM vm;
        vm.id= v;
        vm.speedFactor = speedDist(gen);
        vms.push_back(vm);
    }
}

/**
 * generateTasks
 * -------------
 * Initialises 'numTasks' Task objects with unique IDs and random
 * execution times for each VM. Edge wiring (predecessors/successors)
 * is done separately in generateEdges().
 */
static void generateTasks(std::vector<Task>& tasks, int numTasks,
                           int numVMs, std::mt19937& gen)
{
    std::uniform_real_distribution<double> execDist(EXEC_TIME_MIN, EXEC_TIME_MAX);

    tasks.clear();
    tasks.reserve(numTasks);

    for (int t = 0; t < numTasks; ++t) {
        Task task;
        task.id = t;

        // One execution time entry per VM
        task.execTimes.reserve(numVMs);
        for (int v = 0; v < numVMs; ++v) {
            task.execTimes.push_back(execDist(gen));
        }

        tasks.push_back(task);
    }
}

/**
 * generateEdges
 * -------------
 * Adds directed edges to the DAG using a probabilistic model.
 *
 * For every ordered pair (i, j) with i < j, an edge i→j is created
 * with probability EDGE_PROBABILITY. This ordering GUARANTEES acyclicity
 * without any cycle-detection overhead.
 *
 * After generating all candidate edges, the function guarantees
 * at least one edge exists (adds edge 0→1 if the graph is empty,
 * or 0→(numTasks-1) for single-task-pair graphs).
 *
 * Duplicate edges are prevented by the ordered-pair uniqueness guarantee
 * (each (i,j) pair is visited exactly once).
 */
static void generateEdges(std::vector<Task>& tasks, std::mt19937& gen)
{
    const int numTasks = static_cast<int>(tasks.size());
    if (numTasks < 2) return; // Nothing to connect

    std::uniform_real_distribution<double> probDist(0.0, 1.0);

    int totalEdges = 0;

    for (int i = 0; i < numTasks; ++i) {
        for (int j = i + 1; j < numTasks; ++j) {
            if (probDist(gen) < EDGE_PROBABILITY) {
                // Wire edge i → j
                tasks[i].successors.push_back(j);
                tasks[j].predecessors.push_back(i);
                ++totalEdges;
            }
        }
    }

    // Guarantee at least one edge exists in the graph
    if (totalEdges == 0) {
        int src = 0;
        int dst = numTasks - 1;
        tasks[src].successors.push_back(dst);
        tasks[dst].predecessors.push_back(src);
    }
}

// ============================================================
//  PUBLIC API IMPLEMENTATIONS
// ============================================================

DAGData generateDAG(int numTasks, int numVMs)
{
    // Clamp inputs to sensible minimums
    if (numTasks < 2) numTasks = 2;
    if (numVMs   < 1) numVMs   = 1;

    DAGData dag;
    std::mt19937 gen = makeRandomEngine();

    // Step 1: Build VMs
    generateVMs(dag.vms, numVMs, gen);

    // Step 2: Initialise tasks with exec times (no edges yet)
    generateTasks(dag.tasks, numTasks, numVMs, gen);

    // Step 3: Wire edges (guarantees acyclicity by construction)
    generateEdges(dag.tasks, gen);

    // Step 4: Calculate communication cost factor dynamically
    dag.commCostFactor = calculateCommCostFactor(dag);

    return dag;
}

// ============================================================
//  DYNAMIC COMMUNICATION COST FACTOR CALCULATION
// ============================================================

double calculateCommCostFactor(const DAGData& dag)
{
    const int numTasks = static_cast<int>(dag.tasks.size());
    const int numVMs   = static_cast<int>(dag.vms.size());
    
    if (numTasks < 2 || numVMs < 1) return 0.3; // Default fallback

    // ───────────────────────────────────────────────────────────────
    // 1. TASK GRANULARITY: Ratio of avg exec time to total work
    // ───────────────────────────────────────────────────────────────
    double totalExecTime = 0.0;
    double minExecTime = std::numeric_limits<double>::max();
    double maxExecTime = 0.0;
    
    for (const Task& t : dag.tasks) {
        for (double et : t.execTimes) {
            totalExecTime += et;
            minExecTime = std::min(minExecTime, et);
            maxExecTime = std::max(maxExecTime, et);
        }
    }
    
    double avgExecTime = totalExecTime / (numTasks * numVMs);
    double granularity = avgExecTime / totalExecTime; // Lower = coarser tasks
    
    // ───────────────────────────────────────────────────────────────
    // 2. SYSTEM HETEROGENEITY: Variance in VM speeds
    // ───────────────────────────────────────────────────────────────
    double avgSpeed = 0.0;
    for (const VM& vm : dag.vms) {
        avgSpeed += vm.speedFactor;
    }
    avgSpeed /= numVMs;
    
    double speedVariance = 0.0;
    for (const VM& vm : dag.vms) {
        double diff = vm.speedFactor - avgSpeed;
        speedVariance += diff * diff;
    }
    speedVariance /= numVMs;
    double heterogeneity = std::sqrt(speedVariance) / avgSpeed; // CV of speeds
    
    // ───────────────────────────────────────────────────────────────
    // 3. GRAPH DENSITY: Number of edges vs maximum possible
    // ───────────────────────────────────────────────────────────────
    int totalEdges = 0;
    for (const Task& t : dag.tasks) {
        totalEdges += static_cast<int>(t.successors.size());
    }
    
    int maxPossibleEdges = (numTasks * (numTasks - 1)) / 2; // Complete graph
    double density = static_cast<double>(totalEdges) / maxPossibleEdges;
    
    // ───────────────────────────────────────────────────────────────
    // 4. RESOURCE CONTENTION: Task-to-VM ratio
    // ───────────────────────────────────────────────────────────────
    double taskVmRatio = static_cast<double>(numTasks) / numVMs;
    double contention = std::min(1.0, taskVmRatio / 5.0); // Normalized to [0,1]
    
    // ───────────────────────────────────────────────────────────────
    // 5. COMBINED FACTOR CALCULATION
    // ───────────────────────────────────────────────────────────────
    // Communication cost factor should be:
    //   - LOWER when tasks are coarse-grained (comm cost relatively small)
    //   - LOWER when VM heterogeneity is low (less scheduling variance)
    //   - LOWER when contention is high (comm cost is less relevant)
    //   - HIGHER when graph is dense (more dependencies to consider)
    
    // Invert granularity: finer tasks → higher factor
    double fineness = 1.0 - granularity;
    
    // Weighted combination
   // NOTE: Contention has MULTIPLICATIVE effect to properly handle high-load scenarios
    double baseCommCost = 0.15 +  // Base minimum
                          0.20 * fineness +           // Fine-grained penalty
                          0.15 * heterogeneity +      // Heterogeneity impact
                          0.20 * density;             // Dense graphs
    
    // MULTIPLICATIVE contention factor:
    //   - High contention (0.8-1.0) → multiply by 0.2-0.3 (drastically reduce)
    //   - Medium contention (0.4-0.8) → multiply by 0.5-0.7
    //   - Low contention (0.0-0.4) → multiply by 0.8-1.0
    double contentionFactor = std::max(0.2, 1.0 - 0.8 * contention);  // Range [0.2, 1.0]
    double factor = baseCommCost * contentionFactor;
    
    // Clamp to sensible range [0.1, 0.6]
    factor = std::max(0.1, std::min(0.6, factor));
    
    return factor;
}

// ============================================================
//  VALIDATION
// ============================================================

bool validateDAG(const DAGData& dag)
{
    const int numTasks = static_cast<int>(dag.tasks.size());
    const int numVMs   = static_cast<int>(dag.vms.size());
    bool valid = true;

    // -------------------------------------------------------
    // CHECK A: execTimes size == numVMs for every task
    // -------------------------------------------------------
    for (const Task& t : dag.tasks) {
        if (static_cast<int>(t.execTimes.size()) != numVMs) {
            std::cerr << "[INVALID] Task " << t.id
                      << ": execTimes.size()=" << t.execTimes.size()
                      << " but numVMs=" << numVMs << "\n";
            valid = false;
        }
    }

    for (const Task& task : dag.tasks) {

        // -------------------------------------------------------
        // CHECK B: No self-loops
        // -------------------------------------------------------
        for (int pred : task.predecessors) {
            if (pred == task.id) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has a self-loop in predecessors.\n";
                valid = false;
            }
        }
        for (int succ : task.successors) {
            if (succ == task.id) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has a self-loop in successors.\n";
                valid = false;
            }
        }

        // -------------------------------------------------------
        // CHECK C: All indices are within valid range [0, numTasks)
        // -------------------------------------------------------
        for (int pred : task.predecessors) {
            if (pred < 0 || pred >= numTasks) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has out-of-range predecessor: " << pred << "\n";
                valid = false;
            }
        }
        for (int succ : task.successors) {
            if (succ < 0 || succ >= numTasks) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has out-of-range successor: " << succ << "\n";
                valid = false;
            }
        }

        // -------------------------------------------------------
        // CHECK D: Bidirectional consistency
        //   If task A lists B as successor → B must list A as predecessor.
        //   If task A lists B as predecessor → B must list A as successor.
        // -------------------------------------------------------
        for (int succ : task.successors) {
            if (succ < 0 || succ >= numTasks) continue; // already caught above
            const Task& succTask = dag.tasks[succ];
            bool found = std::find(succTask.predecessors.begin(),
                                   succTask.predecessors.end(),
                                   task.id) != succTask.predecessors.end();
            if (!found) {
                std::cerr << "[INVALID] Bidirectional mismatch: Task " << task.id
                          << " lists " << succ << " as successor, but task "
                          << succ << " does not list " << task.id
                          << " as predecessor.\n";
                valid = false;
            }
        }

        for (int pred : task.predecessors) {
            if (pred < 0 || pred >= numTasks) continue;
            const Task& predTask = dag.tasks[pred];
            bool found = std::find(predTask.successors.begin(),
                                   predTask.successors.end(),
                                   task.id) != predTask.successors.end();
            if (!found) {
                std::cerr << "[INVALID] Bidirectional mismatch: Task " << task.id
                          << " lists " << pred << " as predecessor, but task "
                          << pred << " does not list " << task.id
                          << " as successor.\n";
                valid = false;
            }
        }

        // -------------------------------------------------------
        // CHECK E: Acyclic property (reverse-edge check)
        //   For any edge i→j, we require j > i.
        //   Equivalently: for task i, all successors must have id > i.
        //                             all predecessors must have id < i.
        // -------------------------------------------------------
        for (int succ : task.successors) {
            if (succ >= 0 && succ <= task.id) {
                std::cerr << "[INVALID] Reverse edge detected: Task "
                          << task.id << " → Task " << succ
                          << " violates acyclic ordering (succ <= src).\n";
                valid = false;
            }
        }
        for (int pred : task.predecessors) {
            if (pred >= 0 && pred >= task.id) {
                std::cerr << "[INVALID] Reverse edge detected: Task "
                          << pred << " → Task " << task.id
                          << " violates acyclic ordering (pred >= dst).\n";
                valid = false;
            }
        }

        // -------------------------------------------------------
        // CHECK H: No duplicate edges
        // -------------------------------------------------------
        {
            std::set<int> predSet(task.predecessors.begin(),
                                  task.predecessors.end());
            if (predSet.size() != task.predecessors.size()) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has duplicate entries in predecessors list.\n";
                valid = false;
            }

            std::set<int> succSet(task.successors.begin(),
                                  task.successors.end());
            if (succSet.size() != task.successors.size()) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has duplicate entries in successors list.\n";
                valid = false;
            }
        }
    }

    // -------------------------------------------------------
    // CHECK F: At least one entry node (no predecessors)
    // -------------------------------------------------------
    bool hasEntry = false;
    for (const Task& t : dag.tasks) {
        if (t.predecessors.empty()) { hasEntry = true; break; }
    }
    if (!hasEntry) {
        std::cerr << "[INVALID] DAG has no entry node (all tasks have predecessors).\n";
        valid = false;
    }

    // -------------------------------------------------------
    // CHECK G: At least one exit node (no successors)
    // -------------------------------------------------------
    bool hasExit = false;
    for (const Task& t : dag.tasks) {
        if (t.successors.empty()) { hasExit = true; break; }
    }
    if (!hasExit) {
        std::cerr << "[INVALID] DAG has no exit node (all tasks have successors).\n";
        valid = false;
    }

    return valid;
}

// ============================================================
//  PRINT
// ============================================================

/**
 * Helper: formats a vector<int> as "[ a b c ]" or "[ none ]" if empty.
 */
static std::string formatIntList(const std::vector<int>& v)
{
    if (v.empty()) return "[ none ]";
    std::string out = "[ ";
    for (int x : v) {
        out += std::to_string(x);
        out += ' ';
    }
    out += ']';
    return out;
}

void printDAG(const DAGData& dag)
{
    const std::string line(60, '-');

    // -------------------------------------------------------
    // Print VMs
    // -------------------------------------------------------
    std::cout << "\n" << line << "\n";
    std::cout << "  VIRTUAL MACHINES (" << dag.vms.size() << " total)\n";
    std::cout << line << "\n";
    std::cout << std::fixed << std::setprecision(4);

    for (const VM& vm : dag.vms) {
        std::cout << "  VM " << std::setw(2) << vm.id
                  << "  |  speedFactor = " << vm.speedFactor << "\n";
    }

    // -------------------------------------------------------
    // Print Tasks
    // -------------------------------------------------------
    std::cout << "\n" << line << "\n";
    std::cout << "  TASKS (" << dag.tasks.size() << " total)\n";
    std::cout << line << "\n";

    for (const Task& t : dag.tasks) {
        std::cout << "\n  Task " << std::setw(2) << t.id << "\n";
        std::cout << "    Predecessors : " << formatIntList(t.predecessors) << "\n";
        std::cout << "    Successors   : " << formatIntList(t.successors)   << "\n";
        std::cout << "    Exec Times   : [ ";
        for (size_t v = 0; v < t.execTimes.size(); ++v) {
            std::cout << "VM" << v << "=" << std::setprecision(2)
                      << t.execTimes[v] << "  ";
        }
        std::cout << "]\n";
    }

    std::cout << "\n" << line << "\n\n";
}
