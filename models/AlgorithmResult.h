 

#pragma once

#include <vector>
#include <string>






struct ScheduleEntry {
    int    taskId    = -1;   
    int    vmId      = -1;   
    double startTime  = 0.0; 
    double finishTime = 0.0; 
};






struct AlgorithmResult {
    std::string              algorithmName; 
    std::string              algorithmDesc; 
    std::vector<ScheduleEntry> entries;     
    double makespan   = 0.0;               
    double runtimeMs  = 0.0;               
    bool   isValid    = false;             
};

 


void printAlgorithmResult(const AlgorithmResult& result);