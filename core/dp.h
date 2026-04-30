/**
 * dp.h
 * ----
 * Declares the dp_heft scheduling algorithm.
 *
 * dp_heft implements HEFT enhanced with a Dynamic Programming look-ahead:
 *   - Uses bilateral rank (upRank + downRank) for task prioritisation.
 *   - Uses a 2-task look-ahead DP to choose VM assignments that minimise
 *     the combined finish time of the current task and the next task.
 *
 * Result types are defined globally in AlgorithmResult.h.
 */

#pragma once

#include "dag_generator.h"
#include "AlgorithmResult.h"

// ============================================================
//  PUBLIC API
// ============================================================

/**
 * dp_heft
 * -------
 * Schedules tasks in the DAG using the HEFT + DP algorithm:
 *   1. Compute upward rank and downward rank for each task.
 *   2. Sort tasks by bilateral rank (upRank + downRank) descending.
 *   3. For each task, choose the VM that minimises:
 *         EFT(current task) + 0.25 * best_EFT(next task)
 *      This 2-task look-ahead breaks HEFT's greedy ties and often
 *      finds a better global makespan.
 *
 * @param dag  The fully generated and validated DAGData.
 * @return     An AlgorithmResult with per-task schedule entries and makespan.
 */
AlgorithmResult dp_heft(const DAGData& dag);

/**
 * printAlgorithmResult
 * --------------------
 * Prints a formatted schedule table, makespan, and runtime to stdout.
 *
 * @param result  The AlgorithmResult produced by any scheduling algorithm.
 */
void printAlgorithmResult(const AlgorithmResult& result);
