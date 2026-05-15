/**
 * AlgorithmResult.h
 * -----------------
 * Shared result structures for all scheduling algorithms (HEFT, DP, D&C).
 *
 * Every algorithm in this project produces a schedule — a mapping of tasks
 * to VMs with computed start/finish times. This header defines the common
 * data structures used to represent that output, so all algorithm files
 * share a single, consistent definition.
 *
 * Usage:
 *   #include "AlgorithmResult.h"
 *
 * No Qt. No external libraries. Pure Standard C++11+.
 */

#pragma once

#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <iomanip>
using namespace std;

// ============================================================
//  ScheduleEntry
//  Represents the schedule decision for a single task.
// ============================================================

struct ScheduleEntry {
    int    taskId    = -1;   ///< ID of the scheduled task.
    int    vmId      = -1;   ///< ID of the VM the task is assigned to.
    double startTime  = 0.0; ///< Earliest start time on the assigned VM.
    double finishTime = 0.0; ///< Finish time = startTime + execTime[vmId].
};

// ============================================================
//  AlgorithmResult
//  Top-level result returned by any scheduling algorithm.
// ============================================================

struct AlgorithmResult {
    std::string algorithmName; ///< Human-readable algorithm label.
    std::string algorithmDesc; ///< Short description of the strategy.
    std::vector<ScheduleEntry> entries;     ///< One entry per scheduled task.
    double makespan   = 0.0;               ///< Max finish time across all tasks.
    double runtimeMs  = 0.0;               ///< Wall-clock time to compute schedule (ms).
    bool   isValid    = false;             ///< True if schedule passed sanity checks.
};

/**
 * printAlgorithmResult
 * --------------------
 * Prints a formatted schedule table, makespan, and runtime to stdout.
 *
 * @param result  The AlgorithmResult produced by any scheduling algorithm.
 */



// ============================================================
//  PUBLIC: printAlgorithmResult (for DP results)
// ============================================================

inline void printAlgorithmResult(const AlgorithmResult& result)
{
    const std::string border(60, '=');
    std::cout << "\n" << border << "\n";
    std::cout << "  " << result.algorithmName << "\n";
    std::cout << border << "\n\n";

    // Sort entries by task ID for clean display
    vector<ScheduleEntry> sorted = result.entries;
    sort(sorted.begin(), sorted.end(),
              [](const ScheduleEntry& a, const ScheduleEntry& b){
                  return a.taskId < b.taskId;
              });

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  " << std::setw(8) << "Task"
              << std::setw(8) << "VM"
              << std::setw(12) << "Start"
              << std::setw(12) << "Finish" << "\n";
    std::cout << "  " << std::string(40, '-') << "\n";

    for (const ScheduleEntry& se : sorted) {
        std::cout << "  "
                  << std::setw(8) << se.taskId
                  << std::setw(8) << se.vmId
                  << std::setw(12) << se.startTime
                  << std::setw(12) << se.finishTime << "\n";
    }

    std::cout << "\n  Makespan : " << result.makespan << "\n";
    std::cout << border << "\n\n";
}
