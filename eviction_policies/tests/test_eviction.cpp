#include "lru_policy.h"
#include "lfu_policy.h"
#include "fifo_policy.h"
#include "random_policy.h"
#include "adaptive_policy.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace std;
int main() {
    {
        LRUPolicy p;
        p.onInsert("A");
        p.onInsert("B");
        p.onAccess("A");
        assert(p.selectVictim() == "B");
        cout<<"LRU="<<p.selectVictim()<<endl;
    }

    {
        LFUPolicy p;
        p.onInsert("A");
        p.onInsert("B");
        p.onAccess("A");
        p.onAccess("A");
        assert(p.selectVictim() == "B");
        cout<<"LfU="<<p.selectVictim()<<endl;
    }

    {
        FIFOPolicy p;
        p.onInsert("A");
        p.onInsert("B");
        p.onAccess("B");
        assert(p.selectVictim() == "A");
        cout<<"FIFO="<<p.selectVictim()<<endl;
    }

    {
        RandomPolicy p;
        p.onInsert("A");
        p.onInsert("B");
        const auto victim = p.selectVictim();
        assert(victim == "A" || victim == "B");
        cout<<"Random="<<p.selectVictim()<<endl;
    }

    {
        AdaptivePolicy p;
        p.onInsert("A");
        p.onInsert("B");
        p.onAccess("A");
        const auto victim = p.selectVictim();
        assert(victim == "A" || victim == "B");
        p.registerEvictionDecision(victim);
        p.onRemove(victim);
        cout<<"Adaptive="<<p.selectVictim()<<endl;
    }

    cout << "All tests passed.\n";
    return 0;
}
