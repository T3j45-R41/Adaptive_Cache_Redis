# Cache Eviction Algorithms & Adaptive CACHEUS Engine

This document provides a rigorous mathematical and structural analysis of all five eviction algorithms implemented in the custom C++ Redis-compatible server.

---

## 1. Classical Eviction Algorithms

### A. Least Recently Used (LRU)
- **Data Structures**: `std::list<std::string>` (doubly linked list) + `std::unordered_map<std::string, std::list<std::string>::iterator>`.
- **Complexity**: $O(1)$ lookup, $O(1)$ insertion, $O(1)$ eviction.
- **Mechanics**:
  - Head of the list stores the most recently accessed key; tail stores the least recently accessed key.
  - On `GET` (cache hit): Key node is spliced to the head of the list.
  - On eviction: Key at the tail of the list is popped and removed from the hash map.
- **Limitation**: Vulnerable to scan resistance failure (sequential one-off scans flush out frequently referenced working sets).

---

### B. Least Frequently Used with Recency Tie-Breaking (LFU)
- **Data Structures**:
  - `key_freq`: `std::unordered_map<std::string, uint64_t>` mapping key to access count.
  - `freq_buckets`: `std::unordered_map<uint64_t, std::list<std::string>>` mapping frequency level to doubly linked list of keys.
  - `key_iter`: `std::unordered_map<std::string, std::list<std::string>::iterator>` mapping key to its position inside its frequency bucket.
  - `min_freq`: Scalar tracking lowest populated frequency.
- **Complexity**: Strictly $O(1)$ for access, insertion, and eviction.
- **Tie-Breaking Rule**: When multiple keys share `min_freq`, the least recently accessed key is evicted by popping from the head of `freq_buckets[min_freq]`.
- **Limitation**: High cache pollution when historically popular keys become cold (frequency saturation).

---

### C. First-In First-Out (FIFO)
- **Data Structures**: `std::list<std::string>` (insertion order queue) + `std::unordered_map<std::string, std::list<std::string>::iterator>`.
- **Mechanics**:
  - New keys are appended to the tail of the queue.
  - Cache access (`GET`) does **not** alter the position of the key in the queue.
  - Eviction always removes the oldest inserted item at the head of the queue.
- **Complexity**: $O(1)$ insertion, $O(1)$ eviction.

---

### D. Random Replacement
- **Data Structures**: `std::vector<std::string>` (dense array) + `std::unordered_map<std::string, size_t>` (key to array index).
- **PRNG**: Mersenne Twister `std::mt19937` with configurable seed for deterministic reproducibility.
- **Eviction Mechanics (Swap-and-Pop)**:
  - Generate uniform integer index $i \in [0, N - 1]$.
  - Evict `keys[i]`.
  - Swap `keys[i]` with `keys[N - 1]`, update map index of swapped key, and call `pop_back()`.
- **Complexity**: Strictly $O(1)$ without memory reallocation.

---

## 2. Adaptive CACHEUS-Style Regret Minimization Engine

### A. High-Level Concept
Inspired by adaptive caching literature (LeCaR / CACHEUS), this policy maintains an online ensemble of two competing experts:
- **Expert 1 ($E_1$)**: Recency Expert (LRU)
- **Expert 2 ($E_2$)**: Frequency Expert (LFU)

Rather than committing statically to recency or frequency, the cache observes access patterns in real time, computes empirical regret for each expert, and adjusts expert weights via the **Hedge Multiplicative Weight Update** algorithm.

---

### B. Shadow Histories
To measure counterfactual performance (what would have happened if an expert had been chosen), the policy maintains bounded shadow histories:
- $S_1$: Ghost FIFO queue of keys evicted by Expert 1 (LRU).
- $S_2$: Ghost FIFO queue of keys evicted by Expert 2 (LFU).

Capacity of each shadow history is bounded to cache capacity $C$.

---

### C. Decision & Weight Update Equations

1. **Expert Probabilities**:
   Weights $w_1, w_2$ are initialized to $0.5, 0.5$. The eviction probability for expert $E_i$ is:
   $$P(E_1) = \frac{w_1}{w_1 + w_2}, \quad P(E_2) = \frac{w_2}{w_1 + w_2}$$

2. **Eviction Selection**:
   When cache capacity $C$ is reached, an expert $E \in \{E_1, E_2\}$ is chosen with probability $P(E)$. The chosen expert executes its eviction logic, and the victim is added to its shadow history $S_E$.

3. **Loss & Regret Calculation**:
   When a cache miss occurs on key $k$:
   - If $k \in S_1$: Expert 1 receives credit (it would have preserved $k$ had it been favored); Expert 2 incurs regret:
     $$r_2 = 1.0, \quad r_1 = 0.0$$
   - If $k \in S_2$: Expert 2 receives credit; Expert 1 incurs regret:
     $$r_1 = 1.0, \quad r_2 = 0.0$$
   - If $k \notin S_1 \cup S_2$: Cold miss; $r_1 = 0.0, r_2 = 0.0$.

4. **Multiplicative Weight Update (Hedge Rule)**:
   $$w_i^{(t+1)} = w_i^{(t)} \cdot \exp(-\eta \cdot r_i^{(t)})$$
   where the adaptive learning rate is:
   $$\eta = \sqrt{\frac{8 \ln 2}{C}}$$

5. **Weight Normalization**:
   $$w_1 \leftarrow \frac{w_1}{w_1 + w_2}, \quad w_2 \leftarrow 1.0 - w_1$$

6. **Cumulative Regret Tracking**:
   $$R_i^{(T)} = \sum_{t=1}^{T} r_i^{(t)}$$

---

## 3. Algorithmic Complexity Comparison

| Algorithm | Access Overhead | Eviction Overhead | Memory Overhead | Adaptation Capability |
| :--- | :--- | :--- | :--- | :--- |
| **LRU** | $O(1)$ | $O(1)$ | $O(N)$ list pointers | Static (Recency only) |
| **LFU** | $O(1)$ | $O(1)$ | $O(N)$ freq buckets | Static (Frequency only) |
| **FIFO** | $O(1)$ | $O(1)$ | $O(N)$ queue pointers | Static (Chronological) |
| **Random** | $O(1)$ | $O(1)$ | $O(N)$ vector + index | Static (Uniform) |
| **Adaptive CACHEUS** | $O(1)$ | $O(1)$ | $O(N)$ + $2C$ shadow keys | **Dynamic (Regret Minimizing)** |
