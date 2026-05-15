/**
 * merge.h
 * ----
 * Public interface for merge_schedule: Optimization Algorithm (OP).
 *
 * merge is a hybrid scheduler that adaptively combines the strengths of
 * three existing algorithms in this project:
 *
 *   - DP-HEFT:  bilateral ranking, local look-ahead scheduling
 *   - EDP-HEFT: global DP state optimization for VM assignment
 *   - D&C:     level-based DAG decomposition, parallel subproblem scheduling
 *
 * Algorithm flow:
 *   Phase 1 — Graph Analysis:   classify DAG depth, density, critical path
 *   Phase 2 — Adaptive Scheduling: per-level strategy selection (D&C / DP / EDP)
 *   (No global refinement post-processing is applied.)
 *
 * Result type is the global AlgorithmResult (from AlgorithmResult.h).
 *
 * No Qt. No external libraries. Pure Standard C++17.
 */

#pragma once

#include "dag_generator.h"
#include "AlgorithmResult.h"

/**
 * merge_schedule
 * -----------
 * Schedules tasks in the DAG using the hybrid Optimization Algorithm.
 *
 * @param dag  The fully generated and validated DAGData.
 * @return     An AlgorithmResult with per-task schedule entries and makespan.
 */
AlgorithmResult merge_schedule(const DAGData& dag);
