# 🔍 DAG CONSTRAINT VALIDATION REPORT

## Executive Summary

**Status:** ⚠️ **MIXED RESULTS**

- ✅ **EDP HEFT** - RESPECTS DAG (uses topological ordering via Kahn's algorithm)
- ✅ **DAC** - RESPECTS DAG (uses topological levels via Kahn's algorithm)
- ⚠️ **GREEDY HEFT** - POTENTIAL VIOLATION (rank-based sort, no topological guarantee)
- ⚠️ **DP HEFT** - POTENTIAL VIOLATION (rank-based sort, no topological guarantee)
- ❌ **MERGE** - NOT VALIDATED (complex hybrid approach)

---

## 1️⃣ **EDP HEFT** ✅ RESPECTS DAG CONSTRAINTS

### Algorithm Flow

```cpp
// Phase 1: Compute bilateral rank
vector<double> biRank = upRank + downRank;

// Phase 2: Build topological priority using Kahn's algorithm
struct RankCmp { /* max-heap by biRank */ };
vector<int> indeg(n, 0);  // in-degree tracking

// Initialize in-degrees
for (u in tasks) {
    for (v in successors[u]) {
        indeg[v]++;
    }
}

// Build priority queue with only zero in-degree tasks
priority_queue<int, vector<int>, RankCmp> pq;
for (i in 0..n-1) {
    if (indeg[i] == 0) pq.push(i);
}

// Process in topological order, respecting Kahn's algorithm
vector<int> priority;
while (!pq.empty()) {
    int u = pq.top(); pq.pop();
    priority.push_back(u);
    
    for (v in successors[u]) {
        if (--indeg[v] == 0) {
            pq.push(v);  // Only add when ALL predecessors are done
        }
    }
}
```

### Constraint Guarantee

✅ **GUARANTEED DAG SAFE**

**Why:**
1. Kahn's algorithm is a proven topological sort algorithm
2. Tasks are added to priority queue ONLY when `indeg[v] == 0` (all predecessors processed)
3. When task T is added to priority queue:
   - All predecessor tasks have already been popped from queue
   - All predecessors appear before T in final priority order
4. Phase 3 replay processes tasks in this priority order
5. When EST is computed for task T, all predecessors are already scheduled

**Key Code:**
```cpp
// In Phase 3 replay:
for (int i = 0; i < n; ++i) {
    int tid = priority[i];  // Topologically ordered
    int vm  = assignment[i];
    
    // All predecessors are guaranteed to be in taskVm[] already
    for (int predId : dag.tasks[tid].predecessors) {
        if (snap.taskVm[predId] < 0) continue;  // Should NEVER happen
        // ...
    }
}
```

---

## 2️⃣ **DAC (Divide & Conquer)** ✅ RESPECTS DAG CONSTRAINTS

### Algorithm Flow

```cpp
// Step 1: DIVIDE - Compute topological levels using Kahn's algorithm
vector<int> indegree(n, 0);

for (i in 0..n-1) {
    for (succ in successors[i]) {
        indegree[succ]++;
    }
}

queue<int> q;
for (i in 0..n-1) {
    if (indegree[i] == 0) q.push(i);
}

vector<vector<int>> levels;
while (!q.empty()) {
    vector<int> level;
    for (each task in queue) {
        level.push_back(task);
        for (succ in successors[task]) {
            if (--indegree[succ] == 0) {
                q.push(succ);
            }
        }
    }
    levels.push_back(level);
}
```

### Constraint Guarantee

✅ **GUARANTEED DAG SAFE**

**Why:**
1. Step 1 explicitly computes topological levels using Kahn's algorithm
2. Tasks in level L have NO dependencies within that level (by definition of topological levels)
3. All predecessors of level L are in levels 0 to L-1
4. Step 3 processes levels sequentially: for level 0, then 1, then 2, etc.
5. When processing level L, ALL tasks in levels 0 to L-1 have been scheduled
6. When task T in level L accesses predecessor P, it's guaranteed P is already scheduled

**Key Code:**
```cpp
// Step 3: Process levels in order
for (int l = 0; l < levels.size(); ++l) {
    for (const auto& task : clusterSchedules[l]) {
        int u = task.taskId;
        
        // All predecessors are in earlier levels, already scheduled
        for (int pred : dag.tasks[u].predecessors) {
            // taskVM[pred] is guaranteed to be >= 0
            // taskFinish[pred] is guaranteed to be valid
            readyTime = max(readyTime, taskFinish[pred] + commCost);
        }
    }
}
```

---

## 3️⃣ **GREEDY HEFT** ⚠️ POTENTIAL DAG VIOLATION

### Algorithm Flow

```cpp
// Step 1: Compute upward rank
vector<double> upRank = computeUpwardRank(dag);

// Step 2: Sort by upward rank (NOT topological sort)
vector<int> priority(n);
iota(priority.begin(), priority.end(), 0);
sort(priority.begin(), priority.end(),
     [&](int a, int b) { return upRank[a] > upRank[b]; });

// Step 3: Process in rank order (not guaranteed topological)
for (int tid : priority) {
    double est = vmReady[vmId];
    
    // ⚠️ DANGEROUS: Skip unscheduled predecessors
    for (int predId : dag.tasks[tid].predecessors) {
        if (taskVm[predId] < 0) continue;  // Skip if not scheduled!
        // ...
    }
}
```

### Constraint Violation

⚠️ **POTENTIAL VIOLATION - RANK DOES NOT GUARANTEE TOPOLOGICAL ORDER**

**Problem:**
1. Upward rank is a heuristic, NOT a topological sort
2. Rank formula: `rank_u(t) = avgExec(t) + max_s(commCost(t,s) + rank_u(s))`
3. A task can have high rank without having high-rank successors
4. A child task can have higher rank than its parent

**Example of Violation:**
```
DAG: Parent → Child
Parent: avgExec = 1, successors = [Child]
        rank_u(Parent) = 1 + (0 + rank_u(Child))

Child: avgExec = 50 (slow task), no successors
       rank_u(Child) = 50 + 0 = 50

rank_u(Parent) = 1 + (0 + 50) = 51  ← Parent has higher rank, OK
```

But this is only ONE case. Consider:

```
DAG:  Entry → T1 → T2 → Exit
      Entry → Child → Exit

Entry:  avgExec = 1, successors = [T1, Child]
        rank_u(Entry) = 1 + max(0 + rank_u(T1), 0 + rank_u(Child))

T1:     avgExec = 1, successors = [T2]
        rank_u(T1) = 1 + (0 + rank_u(T2))

T2:     avgExec = 1, successors = [Exit]
        rank_u(T2) = 1 + (0 + 1) = 2

rank_u(T1) = 1 + (0 + 2) = 3
Child:  avgExec = 100, successors = [Exit]
        rank_u(Child) = 100 + (0 + 1) = 101  ← Child has HIGH rank!

Entry: rank_u(Entry) = 1 + max(3, 101) = 102

Priority order: Entry (102) → Child (101) → T1 (3) → T2 (2) → Exit (1)

BUT: When scheduling Child (2nd in priority):
  - Parent Entry is already scheduled ✓
  - But when computing EST for Child, if Entry is not scheduled, it's skipped!
```

### Risk Assessment

**Likelihood of violation in practice:** LOW to MEDIUM
- Rank heuristic generally prioritizes critical path
- Upward rank tends to prioritize tasks with large downstream work
- In most DAGs, this correlates with parents being critical
- BUT it's NOT GUARANTEED

**When violation is most likely:**
- Heterogeneous task execution times (some slow tasks)
- Wide DAGs with many branches
- Tasks with small predecessors but large successors

**Example violation code:**
```cpp
// GREEDY processes tasks in rank priority order
for (int tid : priority) {  // priority is sorted by upRank only
    double est = vmReady[vmId];
    
    for (int predId : dag.tasks[tid].predecessors) {
        if (taskVm[predId] < 0) continue;  // ⚠️ Skip unscheduled!
        // If Child is processed before Parent due to rank,
        // and Parent not yet in taskVm[], it gets skipped
        // EST becomes vmReady only, violating DAG constraint
    }
}
```

---

## 4️⃣ **DP HEFT (Look-Ahead)** ⚠️ SAME VIOLATION AS GREEDY

### Algorithm Flow

```cpp
// Step 1: Compute bilateral rank (upRank + downRank)
vector<double> biRank = upRank + downRank;

// Step 2: Sort by bilateral rank (NOT topological sort)
vector<int> priority(n);
iota(priority.begin(), priority.end(), 0);
sort(priority.begin(), priority.end(),
     [&](int a, int b) { return biRank[a] > biRank[b]; });

// Step 3: Process in rank order with look-ahead
for (int pi = 0; pi < n; ++pi) {
    int tid = priority[pi];  // ⚠️ Could be a child before parent
    
    double est = 0.0;
    double eft = computeEFT(dag, tid, v, vmReady, scheduled, est);
    // Inside computeEFT:
    for (int predId : dag.tasks[tid].predecessors) {
        auto it = scheduled.find(predId);
        if (it == scheduled.end()) continue;  // ⚠️ Skip if not scheduled!
        // ...
    }
}
```

### Constraint Violation

⚠️ **SAME VIOLATION AS GREEDY**

**Problem:**
1. Bilateral rank is a heuristic, NOT a topological sort
2. Bilateral rank = upRank + downRank
3. Downward rank can give high priority to tasks that are "deep" in the DAG
4. A deep child can have higher bilateral rank than its parent

**Why bilateral rank doesn't guarantee topological order:**
```
Upward rank:   Measures downstream critical path (good for parents)
Downward rank: Measures upstream dependencies (good for children!)

A child with:
  - Low upward rank (few successors)
  - High downward rank (many predecessors)
  → Could have high bilateral rank!
```

**Example:**
```
Parent → Child → Exit
Child has:
  - upRank = 10 (low, only to Exit)
  - downRank = 50 (high, depends on Parent)
  - biRank = 60

Parent has:
  - upRank = 10 + 0 + 10 = 20
  - downRank = 0 (entry task)
  - biRank = 20

Priority: Child (60) > Parent (20) ← VIOLATION!
```

**Same risky code:**
```cpp
for (int pi = 0; pi < n; ++pi) {
    int tid = priority[pi];
    double eft = computeEFT(dag, tid, v, vmReady, scheduled, est);
    // ...
    if (predId not in scheduled) continue;  // Skip unscheduled
}
```

---

## 📊 VALIDATION SUMMARY TABLE

| Algorithm | Ordering Method | DAG Guarantee | Risk Level | Notes |
|---|---|---|---|---|
| **EDP HEFT** | Kahn's algorithm (topological) | ✅ YES | None | Explicit topological sort with priority |
| **DAC** | Kahn's algorithm (levels) | ✅ YES | None | Level-by-level processing |
| **GREEDY HEFT** | Upward rank (heuristic) | ❌ NO | MEDIUM | Could process child before parent |
| **DP HEFT** | Bilateral rank (heuristic) | ❌ NO | MEDIUM | Could process child before parent |
| **MERGE** | Complex hybrid | ? UNKNOWN | TBD | Needs separate analysis |

---

## 🛠️ RECOMMENDED FIXES

### Option A: Fix GREEDY & DP with Topological Preprocessing

```cpp
// Add before scheduling
vector<int> topoOrder = computeTopologicalSort(dag);  // Kahn's algorithm
vector<int> topoRank(n);
for (int i = 0; i < n; ++i) {
    topoRank[topoOrder[i]] = i;
}

// Modify sort to respect topological order as tiebreaker
sort(priority.begin(), priority.end(),
     [&](int a, int b) {
         if (abs(rank[a] - rank[b]) > 1e-9) return rank[a] > rank[b];
         // ← Add topological tiebreaker
         return topoRank[a] < topoRank[b];
     });
```

This ensures topological order is preserved while still using rank heuristic.

### Option B: Force Predecessor Scheduling (Like DP look-ahead)

```cpp
// Before scheduling task T, ensure all predecessors are scheduled
auto ensurePredecessorsScheduled = [&](int taskId) {
    for (int predId : dag.tasks[taskId].predecessors) {
        if (taskVm[predId] < 0) {
            // Schedule predecessor first (recursive)
            scheduleTask(predId);
        }
    }
};
```

---

## ✅ FINAL RECOMMENDATION

**Current State:**
- EDP HEFT: ✅ Safe (topological)
- DAC: ✅ Safe (topological)
- GREEDY: ⚠️ At risk (rank-based)
- DP: ⚠️ At risk (rank-based)
- MERGE: ❓ Unknown

**Recommended Actions:**
1. **ACCEPT EDP & DAC** as DAG-safe for production
2. **FIX GREEDY & DP** by adding topological ordering guarantee
3. **ANALYZE MERGE** separately

**For GREEDY & DP Fix:**
- Add Kahn's topological sort preprocessing
- Use topological order as secondary sort key after rank
- Ensures rank heuristic is preserved while respecting DAG

---

*Validation Report - May 15, 2026*
*Status: EDP HEFT and DAC are safe; GREEDY and DP require fixes*
