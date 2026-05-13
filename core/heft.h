/**
 * heft.h
 * ------
 * Public interface for greedy HEFT scheduling.
 */

#pragma once

#include "dag_generator.h"
#include "AlgorithmResult.h"

/**
 * heft_schedule
 * -------------
 * Standard HEFT with greedy VM selection (minimum EFT).
 *
 * @param dag  The fully generated and validated DAGData.
 * @return     An AlgorithmResult with per-task schedule entries and makespan.
 */
AlgorithmResult heft_schedule(const DAGData& dag);
