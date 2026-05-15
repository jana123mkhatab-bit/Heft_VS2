 

#pragma once

#include <vector>
#include <string>






constexpr int DEFAULT_NUM_TASKS = 40;


constexpr int DEFAULT_NUM_VMS = 7;


constexpr double EDGE_PROBABILITY = 0.35;


constexpr double EXEC_TIME_MIN = 1.0;
constexpr double EXEC_TIME_MAX = 20.0;


constexpr double SPEED_MIN = 0.5;
constexpr double SPEED_MAX = 2.0;





 
struct Task {
    int id;                        
    std::vector<int> predecessors; 
    std::vector<int> successors;   
    std::vector<double> execTimes; 
};

 
struct VM {
    int id;              
    double speedFactor;  
};

 
struct DAGData {
    std::vector<Task> tasks; 
    std::vector<VM>   vms;   
    double commCostFactor;   
};





 
DAGData generateDAG(int numTasks = DEFAULT_NUM_TASKS,
                    int numVMs   = DEFAULT_NUM_VMS);

 
double calculateCommCostFactor(const DAGData& dag);

 
bool validateDAG(const DAGData& dag);

 
void printDAG(const DAGData& dag);
