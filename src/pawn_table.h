#pragma once

#include "types.h"
#include <cstring>

namespace NextFish {

struct PawnStructureEntry {
    Key key = 0;
    int16_t w_mg = 0;
    int16_t w_eg = 0;
    int16_t b_mg = 0;
    int16_t b_eg = 0;
};

class PawnTable {
public:
    static constexpr size_t TABLE_SIZE = 16384; // 16K entries * 16 bytes = 256 KB (resident in L2 cache)
    static constexpr size_t MASK = TABLE_SIZE - 1;

    PawnTable();
    void clear();

    inline bool probe(Key key, int16_t& w_mg, int16_t& w_eg, int16_t& b_mg, int16_t& b_eg) const {
        const PawnStructureEntry& e = entries[key & MASK];
        if (e.key == key) {
            w_mg = e.w_mg;
            w_eg = e.w_eg;
            b_mg = e.b_mg;
            b_eg = e.b_eg;
            return true;
        }
        return false;
    }

    inline void store(Key key, int16_t w_mg, int16_t w_eg, int16_t b_mg, int16_t b_eg) {
        PawnStructureEntry& e = entries[key & MASK];
        e.key = key;
        e.w_mg = w_mg;
        e.w_eg = w_eg;
        e.b_mg = b_mg;
        e.b_eg = b_eg;
    }

private:
    PawnStructureEntry entries[TABLE_SIZE];
};

extern PawnTable GlobalPawnTable;

} // namespace NextFish
