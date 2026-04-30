#pragma once

#include "dag_generator.h"
#include "AlgorithmResult.h"

/**
 * dac_schedule
 * ------------
 * Schedules tasks in the DAG using a Divide & Conquer approach:
 *   1. DIVIDE: Partition the DAG into independent clusters (topological levels).
 *   2. CONQUER: Schedule each cluster independently.
 *   3. MERGE: Combine partial schedules, recomputing valid start/finish times
 *      to respect dependencies and VM availability.
 *
 * @param dag  The fully generated and validated DAGData.
 * @return     An AlgorithmResult with per-task schedule entries and makespan.
 */
AlgorithmResult dac_schedule(const DAGData& dag);