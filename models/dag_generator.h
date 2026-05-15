 

#pragma once

// Part: Includes
#include <vector>
#include <string>



// Part: Constants
constexpr int DEFAULT_NUM_TASKS = 40;

// Part: Constants
constexpr int DEFAULT_NUM_VMS = 7;

// Part: Constants
constexpr double EDGE_PROBABILITY = 0.35;

// Part: Constants
constexpr double EXEC_TIME_MIN = 1.0;
constexpr double EXEC_TIME_MAX = 20.0;

// Part: Constants
constexpr double SPEED_MIN = 0.5;
constexpr double SPEED_MAX = 2.0;


// Part: Types
struct Task {
    int id;                        
    std::vector<int> predecessors; 
    std::vector<int> successors;   
    std::vector<double> execTimes; 
};
// Part: Types
struct VM {
    int id;              
    double speedFactor;  
};
// Part: Types
struct DAGData {
    std::vector<Task> tasks; 
    std::vector<VM>   vms;   
    double commCostFactor;   
};


// Part: Public API
DAGData generateDAG(int numTasks = DEFAULT_NUM_TASKS,
                    int numVMs   = DEFAULT_NUM_VMS);
// Part: Public API
double calculateCommCostFactor(const DAGData& dag);
// Part: Public API
bool validateDAG(const DAGData& dag);
// Part: Public API
void printDAG(const DAGData& dag);
