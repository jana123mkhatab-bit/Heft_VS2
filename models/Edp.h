/**
 * Edp.h
 * -----
 * Public interface for edp_heft: Enhanced Dynamic Programming HEFT.
 *
 * edp_heft returns the global AlgorithmResult type (from AlgorithmResult.h)
 * so it can be printed with the same printAlgorithmResult() used by all
 * other algorithms in this project.
 *
 * No Qt. No external libraries. Pure Standard C++11+.
 */

#pragma once

#include "dag_generator.h"    // DAGData, Task, VM
#include "AlgorithmResult.h"  // ScheduleEntry, AlgorithmResult

// ── Algorithm entry point ─────────────────────────────────────────────────────
// Runs the enhanced DP HEFT algorithm and returns a populated AlgorithmResult.
AlgorithmResult edp_heft(const DAGData& dag);
