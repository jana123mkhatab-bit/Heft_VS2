 

#include "dag_generator.h"

#include <iostream>   
#include <iomanip>    
#include <random>     
#include <set>        
#include <algorithm>  
#include <cmath>      
#include <limits>     





 
static std::mt19937 makeRandomEngine()
{
    
    
    constexpr unsigned int FIXED_SEED = 42u;
    return std::mt19937(FIXED_SEED);
}

 
static void generateVMs(std::vector<VM>& vms, int numVMs, std::mt19937& gen)
{
    std::uniform_real_distribution<double> speedDist(SPEED_MIN, SPEED_MAX);

    vms.clear();
    vms.reserve(numVMs);

    for (int v = 0; v < numVMs; ++v) {
        VM vm;
        vm.id= v;
        vm.speedFactor = speedDist(gen);
        vms.push_back(vm);
    }
}

 
static void generateTasks(std::vector<Task>& tasks, int numTasks,
                           int numVMs, std::mt19937& gen)
{
    std::uniform_real_distribution<double> execDist(EXEC_TIME_MIN, EXEC_TIME_MAX);

    tasks.clear();
    tasks.reserve(numTasks);

    for (int t = 0; t < numTasks; ++t) {
        Task task;
        task.id = t;

        
        task.execTimes.reserve(numVMs);
        for (int v = 0; v < numVMs; ++v) {
            task.execTimes.push_back(execDist(gen));
        }

        tasks.push_back(task);
    }
}

 
static void generateEdges(std::vector<Task>& tasks, std::mt19937& gen)
{
    const int numTasks = static_cast<int>(tasks.size());
    if (numTasks < 2) return; 

    std::uniform_real_distribution<double> probDist(0.0, 1.0);

    int totalEdges = 0;

    for (int i = 0; i < numTasks; ++i) {
        for (int j = i + 1; j < numTasks; ++j) {
            if (probDist(gen) < EDGE_PROBABILITY) {
                
                tasks[i].successors.push_back(j);
                tasks[j].predecessors.push_back(i);
                ++totalEdges;
            }
        }
    }

    
    if (totalEdges == 0) {
        int src = 0;
        int dst = numTasks - 1;
        tasks[src].successors.push_back(dst);
        tasks[dst].predecessors.push_back(src);
    }
}





DAGData generateDAG(int numTasks, int numVMs)
{
    
    if (numTasks < 2) numTasks = 2;
    if (numVMs   < 1) numVMs   = 1;

    DAGData dag;
    std::mt19937 gen = makeRandomEngine();

    
    generateVMs(dag.vms, numVMs, gen);

    
    generateTasks(dag.tasks, numTasks, numVMs, gen);

    
    generateEdges(dag.tasks, gen);

    
    dag.commCostFactor = calculateCommCostFactor(dag);

    return dag;
}





double calculateCommCostFactor(const DAGData& dag)
{
    const int numTasks = static_cast<int>(dag.tasks.size());
    const int numVMs   = static_cast<int>(dag.vms.size());
    
    if (numTasks < 2 || numVMs < 1) return 0.3; 

    
    
    
    double totalExecTime = 0.0;
    double minExecTime = std::numeric_limits<double>::max();
    double maxExecTime = 0.0;
    
    for (const Task& t : dag.tasks) {
        for (double et : t.execTimes) {
            totalExecTime += et;
            minExecTime = std::min(minExecTime, et);
            maxExecTime = std::max(maxExecTime, et);
        }
    }
    
    double avgExecTime = totalExecTime / (numTasks * numVMs);
    double granularity = avgExecTime / totalExecTime; 
    
    
    
    
    double avgSpeed = 0.0;
    for (const VM& vm : dag.vms) {
        avgSpeed += vm.speedFactor;
    }
    avgSpeed /= numVMs;
    
    double speedVariance = 0.0;
    for (const VM& vm : dag.vms) {
        double diff = vm.speedFactor - avgSpeed;
        speedVariance += diff * diff;
    }
    speedVariance /= numVMs;
    double heterogeneity = std::sqrt(speedVariance) / avgSpeed; 
    
    
    
    
    int totalEdges = 0;
    for (const Task& t : dag.tasks) {
        totalEdges += static_cast<int>(t.successors.size());
    }
    
    int maxPossibleEdges = (numTasks * (numTasks - 1)) / 2; 
    double density = static_cast<double>(totalEdges) / maxPossibleEdges;
    
    
    
    
    double taskVmRatio = static_cast<double>(numTasks) / numVMs;
    double contention = std::min(1.0, taskVmRatio / 5.0); 
    
    
    
    
    
    
    
    
    
    
    
    double fineness = 1.0 - granularity;
    
    
   
    double baseCommCost = 0.15 +  
                          0.20 * fineness +           
                          0.15 * heterogeneity +      
                          0.20 * density;             
    
    
    
    
    
    double contentionFactor = std::max(0.2, 1.0 - 0.8 * contention);  
    double factor = baseCommCost * contentionFactor;
    
    
    factor = std::max(0.1, std::min(0.6, factor));
    
    return factor;
}





bool validateDAG(const DAGData& dag)
{
    const int numTasks = static_cast<int>(dag.tasks.size());
    const int numVMs   = static_cast<int>(dag.vms.size());
    bool valid = true;

    
    
    
    for (const Task& t : dag.tasks) {
        if (static_cast<int>(t.execTimes.size()) != numVMs) {
            std::cerr << "[INVALID] Task " << t.id
                      << ": execTimes.size()=" << t.execTimes.size()
                      << " but numVMs=" << numVMs << "\n";
            valid = false;
        }
    }

    for (const Task& task : dag.tasks) {

        
        
        
        for (int pred : task.predecessors) {
            if (pred == task.id) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has a self-loop in predecessors.\n";
                valid = false;
            }
        }
        for (int succ : task.successors) {
            if (succ == task.id) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has a self-loop in successors.\n";
                valid = false;
            }
        }

        
        
        
        for (int pred : task.predecessors) {
            if (pred < 0 || pred >= numTasks) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has out-of-range predecessor: " << pred << "\n";
                valid = false;
            }
        }
        for (int succ : task.successors) {
            if (succ < 0 || succ >= numTasks) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has out-of-range successor: " << succ << "\n";
                valid = false;
            }
        }

        
        
        
        
        
        for (int succ : task.successors) {
            if (succ < 0 || succ >= numTasks) continue; 
            const Task& succTask = dag.tasks[succ];
            bool found = std::find(succTask.predecessors.begin(),
                                   succTask.predecessors.end(),
                                   task.id) != succTask.predecessors.end();
            if (!found) {
                std::cerr << "[INVALID] Bidirectional mismatch: Task " << task.id
                          << " lists " << succ << " as successor, but task "
                          << succ << " does not list " << task.id
                          << " as predecessor.\n";
                valid = false;
            }
        }

        for (int pred : task.predecessors) {
            if (pred < 0 || pred >= numTasks) continue;
            const Task& predTask = dag.tasks[pred];
            bool found = std::find(predTask.successors.begin(),
                                   predTask.successors.end(),
                                   task.id) != predTask.successors.end();
            if (!found) {
                std::cerr << "[INVALID] Bidirectional mismatch: Task " << task.id
                          << " lists " << pred << " as predecessor, but task "
                          << pred << " does not list " << task.id
                          << " as successor.\n";
                valid = false;
            }
        }

        
        
        
        
        
        
        for (int succ : task.successors) {
            if (succ >= 0 && succ <= task.id) {
                std::cerr << "[INVALID] Reverse edge detected: Task "
                          << task.id << " → Task " << succ
                          << " violates acyclic ordering (succ <= src).\n";
                valid = false;
            }
        }
        for (int pred : task.predecessors) {
            if (pred >= 0 && pred >= task.id) {
                std::cerr << "[INVALID] Reverse edge detected: Task "
                          << pred << " → Task " << task.id
                          << " violates acyclic ordering (pred >= dst).\n";
                valid = false;
            }
        }

        
        
        
        {
            std::set<int> predSet(task.predecessors.begin(),
                                  task.predecessors.end());
            if (predSet.size() != task.predecessors.size()) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has duplicate entries in predecessors list.\n";
                valid = false;
            }

            std::set<int> succSet(task.successors.begin(),
                                  task.successors.end());
            if (succSet.size() != task.successors.size()) {
                std::cerr << "[INVALID] Task " << task.id
                          << " has duplicate entries in successors list.\n";
                valid = false;
            }
        }
    }

    
    
    
    bool hasEntry = false;
    for (const Task& t : dag.tasks) {
        if (t.predecessors.empty()) { hasEntry = true; break; }
    }
    if (!hasEntry) {
        std::cerr << "[INVALID] DAG has no entry node (all tasks have predecessors).\n";
        valid = false;
    }

    
    
    
    bool hasExit = false;
    for (const Task& t : dag.tasks) {
        if (t.successors.empty()) { hasExit = true; break; }
    }
    if (!hasExit) {
        std::cerr << "[INVALID] DAG has no exit node (all tasks have successors).\n";
        valid = false;
    }

    return valid;
}





 
static std::string formatIntList(const std::vector<int>& v)
{
    if (v.empty()) return "[ none ]";
    std::string out = "[ ";
    for (int x : v) {
        out += std::to_string(x);
        out += ' ';
    }
    out += ']';
    return out;
}

void printDAG(const DAGData& dag)
{
    const std::string line(60, '-');

    
    
    
    std::cout << "\n" << line << "\n";
    std::cout << "  VIRTUAL MACHINES (" << dag.vms.size() << " total)\n";
    std::cout << line << "\n";
    std::cout << std::fixed << std::setprecision(4);

    for (const VM& vm : dag.vms) {
        std::cout << "  VM " << std::setw(2) << vm.id
                  << "  |  speedFactor = " << vm.speedFactor << "\n";
    }

    
    
    
    std::cout << "\n" << line << "\n";
    std::cout << "  TASKS (" << dag.tasks.size() << " total)\n";
    std::cout << line << "\n";

    for (const Task& t : dag.tasks) {
        std::cout << "\n  Task " << std::setw(2) << t.id << "\n";
        std::cout << "    Predecessors : " << formatIntList(t.predecessors) << "\n";
        std::cout << "    Successors   : " << formatIntList(t.successors)   << "\n";
        std::cout << "    Exec Times   : [ ";
        for (size_t v = 0; v < t.execTimes.size(); ++v) {
            std::cout << "VM" << v << "=" << std::setprecision(2)
                      << t.execTimes[v] << "  ";
        }
        std::cout << "]\n";
    }

    std::cout << "\n" << line << "\n\n";
}
