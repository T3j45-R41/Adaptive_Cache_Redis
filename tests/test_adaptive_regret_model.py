import math
import pytest

class AdaptivePolicySimulator:
    """
    Python reference model of AdaptiveEvictionPolicy implementing LeCaR/ARC-style
    multiplicative weights update with bounded shadow histories.
    """
    def __init__(self, capacity=10, learning_rate=0.05):
        self.capacity = capacity
        self.eta = learning_rate
        self.w1 = 0.5  # LRU weight
        self.w2 = 0.5  # LFU weight
        self.cum_regret1 = 0.0
        self.cum_regret2 = 0.0
        self.shadow1 = set()  # Keys evicted by LRU
        self.shadow2 = set()  # Keys evicted by LFU

    def record_eviction(self, expert: str, key: str):
        if expert == "LRU":
            self.shadow1.add(key)
        else:
            self.shadow2.add(key)

    def on_miss_or_insert(self, key: str):
        r1 = 0.0
        r2 = 0.0
        feedback = False

        if key in self.shadow1:
            # Key was previously evicted by Expert 1 (LRU).
            # Because it is requested again, LRU made the mistake.
            # Expert 1 incurs regret r1 = 1.0 (lowering w1 and raising w2).
            r1 = 1.0
            feedback = True
            self.shadow1.remove(key)
        elif key in self.shadow2:
            # Key was previously evicted by Expert 2 (LFU).
            # Because it is requested again, LFU made the mistake.
            # Expert 2 incurs regret r2 = 1.0 (lowering w2 and raising w1).
            r2 = 1.0
            feedback = True
            self.shadow2.remove(key)

        if feedback:
            self.cum_regret1 += r1
            self.cum_regret2 += r2
            self.w1 *= math.exp(-self.eta * r1)
            self.w2 *= math.exp(-self.eta * r2)
            self.w1 = max(0.001, self.w1)
            self.w2 = max(0.001, self.w2)
            total = self.w1 + self.w2
            self.w1 /= total
            self.w2 /= total

        return feedback, r1, r2


def test_lecar_regret_inversion_fix():
    """
    Verifies that when a key in shadow1 (evicted by LRU) is requested again,
    LRU incurs regret (r1=1.0) and w1 decreases while w2 increases.
    """
    sim = AdaptivePolicySimulator(capacity=5, learning_rate=0.1)
    assert sim.w1 == 0.5
    assert sim.w2 == 0.5

    # Simulate LRU evicting 'stock:AAPL'
    sim.record_eviction("LRU", "stock:AAPL")
    assert "stock:AAPL" in sim.shadow1

    # Re-access 'stock:AAPL' -> LRU made the mistake
    feedback, r1, r2 = sim.on_miss_or_insert("stock:AAPL")
    assert feedback is True
    assert r1 == 1.0
    assert r2 == 0.0

    # w1 must decrease, w2 must increase
    assert sim.w1 < 0.5, f"Expected w1 < 0.5, got {sim.w1}"
    assert sim.w2 > 0.5, f"Expected w2 > 0.5, got {sim.w2}"
    assert math.isclose(sim.w1 + sim.w2, 1.0, rel_tol=1e-6)
    assert sim.cum_regret1 == 1.0
    assert sim.cum_regret2 == 0.0


def test_lfu_shadow2_regret():
    """
    Verifies that when a key in shadow2 (evicted by LFU) is requested again,
    LFU incurs regret (r2=1.0) and w2 decreases while w1 increases.
    """
    sim = AdaptivePolicySimulator(capacity=5, learning_rate=0.1)

    # Simulate LFU evicting 'stock:MSFT'
    sim.record_eviction("LFU", "stock:MSFT")
    assert "stock:MSFT" in sim.shadow2

    # Re-access 'stock:MSFT' -> LFU made the mistake
    feedback, r1, r2 = sim.on_miss_or_insert("stock:MSFT")
    assert feedback is True
    assert r1 == 0.0
    assert r2 == 1.0

    # w2 must decrease, w1 must increase
    assert sim.w2 < 0.5, f"Expected w2 < 0.5, got {sim.w2}"
    assert sim.w1 > 0.5, f"Expected w1 > 0.5, got {sim.w1}"
    assert math.isclose(sim.w1 + sim.w2, 1.0, rel_tol=1e-6)
    assert sim.cum_regret2 == 1.0
    assert sim.cum_regret1 == 0.0


def test_scan_vs_loop_weight_adaptation():
    """
    Simulates a cyclic loop workload (1..C+1 repeating) where pure LRU
    consistently evicts the next-needed key.
    The adaptive learner must penalize LRU repeatedly and adapt toward LFU.
    """
    capacity = 5
    sim = AdaptivePolicySimulator(capacity=capacity, learning_rate=0.2)

    # In a cyclic loop 1..6 with capacity 5:
    # Key 1 is evicted by LRU. Key 2 is evicted by LRU, etc.
    # As each evicted key is immediately accessed on the next cycle,
    # LRU repeatedly incurs regret.
    for cycle in range(5):
        evicted_key = f"key_{cycle}"
        sim.record_eviction("LRU", evicted_key)
        feedback, r1, r2 = sim.on_miss_or_insert(evicted_key)
        assert feedback is True
        assert r1 == 1.0

    # After 5 consecutive LRU mistakes, w1 must have dropped significantly
    assert sim.w1 < 0.35, f"Expected w1 < 0.35 after repeated LRU misses, got {sim.w1}"
    assert sim.w2 > 0.65, f"Expected w2 > 0.65 favoring frequency, got {sim.w2}"
    assert sim.cum_regret1 == 5.0
    assert sim.cum_regret2 == 0.0
