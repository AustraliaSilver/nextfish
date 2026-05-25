#include "harenn_ctrl.h"
#include "harenn.h"
#include "bitboard.h"
#include "position.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Stockfish {

namespace HARENN {

namespace {
    constexpr int CACHE_SIZE = 2048;
    
    struct LRUCache {
        struct Entry {
            Key key = 0;
            EvalResult result;
        };
        std::array<Entry, CACHE_SIZE> table;
        
        LRUCache() {
            // entries default initialized
        }
        
        EvalResult* lookup(Key key) {
            int idx = (int)(key & (CACHE_SIZE - 1));
            if (table[idx].key == key) {
                return &table[idx].result;
            }
            return nullptr;
        }
        
        void insert(Key key, EvalResult res) {
            int idx = (int)(key & (CACHE_SIZE - 1));
            table[idx].key = key;
            table[idx].result = res;
        }
    };
    thread_local LRUCache nodeCache;
}

void Controller::init() {
    GuidanceProvider::init();
}

EvalResult Controller::get_analysis(const Position& pos) {
    Key key = pos.key();
    EvalResult* cached = nodeCache.lookup(key);
    if (cached)
        return *cached;

    EvalResult res = GuidanceProvider::query(pos);
    nodeCache.insert(key, res);
    return res;
}

int Controller::get_smart_reduction(const Position& pos, Depth depth, Move m, int moveCount, int baseR, Value staticEval, Value rootScore) {
    // V70: Smart LMR disabled to keep search loop 100% Stockfish pristine.
    // Dynamic time management is kept as the sole advisor component.
    (void)pos; (void)depth; (void)m; (void)moveCount; (void)staticEval; (void)rootScore;
    return baseR;
}

int Controller::get_move_bonus(const Position& pos, Move m) {
    (void)pos; (void)m;
    return 0;
}

int Controller::adjust_aspiration(const Position& pos, int delta) {
    // V69: Aspiration adjustment disabled for LTC. Default Stockfish aspiration
    // window is highly optimized for deep searches; modifying it causes search instability.
    (void)pos;
    return delta;
}

int Controller::get_qs_tactical_adjustment(const Position& pos, int standPat) {
    (void)pos;
    return standPat;
}

int Controller::get_time_multiplier(const Position& pos) {
    // V72: Centered Dynamic Time Boosting.
    
    // Safety check: if HARENN model fails to load, fallback to standard Stockfish (100% time usage)
    if (!GuidanceProvider::is_model_loaded()) {
        return 100;
    }
    
    // Normalize each head individually based on empirical ranges observed over opening book positions.
    EvalResult res = get_analysis(pos);
    
    float cn = (res.tau - 0.2076f) / 0.2785f;
    float crn = (res.rho - 0.2331f) / 0.1533f;
    float dn = (res.rs - 0.1250f) / 0.1452f;
    
    // Clamp each normalized head to [0.0f, 1.0f]
    cn = std::max(0.0f, std::min(1.0f, cn));
    crn = std::max(0.0f, std::min(1.0f, crn));
    dn = std::max(0.0f, std::min(1.0f, dn));
    
    // Use the maximum of the three normalized heads to represent the overall complexity/risk.
    // Since comp, crit, and diff are negatively correlated, their sum cancels out, but their max
    // accurately captures when at least one aspect of the position is critical or complex.
    float max_val = std::max({cn, crn, dn});
    
    // Scale centered around 0.75f with a factor of 200.0f
    int mult = 100 + static_cast<int>((max_val - 0.75f) * 200.0f);
    
    // Clamp the multiplier between 80% (save time on very simple positions) 
    // and 150% (spend extra time on complex/critical positions).
    // Average multiplier across opening games is ~118%, which yields a healthy but sustainable time usage.
    return std::max(80, std::min(150, mult));
}

} // namespace HARENN

} // namespace Stockfish
