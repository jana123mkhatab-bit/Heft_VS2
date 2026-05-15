 

#pragma once

// Part: Includes
#include <vector>
#include <string>



// Part: Types
struct ScheduleEntry {
    int    taskId    = -1;   
    int    vmId      = -1;   
    double startTime  = 0.0; 
    double finishTime = 0.0; 
};



// Part: Types
struct AlgorithmResult {
    std::string              algorithmName; 
    std::string              algorithmDesc; 
    std::vector<ScheduleEntry> entries;     
    double makespan   = 0.0;               
    double runtimeMs  = 0.0;               
    bool   isValid    = false;             
};

// Part: Public API
void printAlgorithmResult(const AlgorithmResult& result);