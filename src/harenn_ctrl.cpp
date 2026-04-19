#include "harenn_ctrl.h"
#include "harenn.h"
#include "bitboard.h"
#include "position.h"
#include <algorithm>
#include <cmath>

namespace Stockfish {

namespace HARENN {

namespace {
    constexpr int CACHE_SIZE = 16;
    
    struct LRUCache {
        std::array<Key, CACHE_SIZE> keys;
        std::array<EvalResult, CACHE_SIZE> results;
        std::array<uint64_t, CACHE_SIZE> timestamps;
        uint64_t counter = 0;
        
        LRUCache() {
            keys.fill(0);
            timestamps.fill(0);
        }
        
        EvalResult* lookup(Key key) {
            uint64_t min_ts = timestamps[0];
            int min_idx = 0;
            
            for (int i = 0; i < CACHE_SIZE; ++i) {
                if (keys[i] == key) {
                    timestamps[i] = ++counter; // Move to front
                    return &results[i];
                }
                if (timestamps[i] < min_ts) {
                    min_ts = timestamps[i];
                    min_idx = i;
                }
            }
            return nullptr;
        }
        
        void insert(Key key, EvalResult res) {
            // Find LRU entry
            uint64_t min_ts = timestamps[0];
            int min_idx = 0;
            for (int i = 1; i < CACHE_SIZE; ++i) {
                if (timestamps[i] < min_ts) {
                    min_ts = timestamps[i];
                    min_idx = i;
                }
            }
            
            keys[min_idx] = key;
            results[min_idx] = res;
            timestamps[min_idx] = ++counter;
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

int Controller::get_smart_reduction(const Position& pos, Depth depth, Move m, int moveCount, int baseR) {
    // V31: AI Strategic Orchestrator
    // Depth threshold 8 - optimal for bullet time control
    // We target depth >= 8 and critical moves. 
    if (!m || depth < 8 || moveCount < 3 || pos.capture_stage(m))
        return baseR;

    EvalResult res = get_analysis(pos);
    int adj = 0;

    // 1. Unified Volatility Guard (Combined Tau and Rho)
    // Bullet-optimized: Higher threshold (0.70f) and smaller adjustment (128)
    // Less aggressive to avoid over-reducing depth in bullet
    float volatility = (res.tau * 3.15f) + (res.rho * 0.25f); 
    if (volatility > 0.70f) // Increased threshold for bullet
        adj -= 128; // Smaller depth adjustment for bullet

    // 2. Endgame Glide (Using RS)
    // If it's a deep endgame and AI says it's quiet, speed up.
    // More aggressive for bullet to save time
    if (res.rs > 0.90f && res.tau < 0.15f)
        adj += 96; // Increased speed-up for quiet endgames

    return std::max(0, baseR + adj);
}

int Controller::get_move_bonus(const Position& pos, Move m) {
    (void)pos; (void)m;
    return 0;
}

int Controller::adjust_aspiration(const Position& pos, int delta) {
    EvalResult res = get_analysis(pos);
    // V54: Enhanced Aspiration for Black and High Risk positions
    // Black needs a wider window to catch White's aggressive thrusts
    if (res.rho > 0.80f || (pos.side_to_move() == BLACK && res.eval < -50.0f))
        return delta + 3;
    return delta + 2;
}

// Function to check consensus between AI and Engine
int Controller::get_qs_tactical_adjustment(const Position& pos, int standPat) {
    EvalResult res = get_analysis(pos);
    
    // If AI evaluation significantly disagrees with the engine's stand-pat
    // (Meaning there's likely a hidden tactical resource AI sees)
    if (std::abs(res.eval - (float)standPat) > 180.0f) {
        // Return a small penalty to force engine to search captures
        return standPat - 15; 
    }
    
    return standPat;
}

int Controller::get_time_multiplier(const Position& pos) {
    EvalResult res = get_analysis(pos);
    
    // Base multiplier is 100 (normal time)
    int multiplier = 100;
    
    // 1. Endgame Glide - save time in quiet endgames
    // If AI says it's quiet endgame (high rs, low tau), use less time
    if (res.rs > 0.85f && res.tau < 0.20f)
        multiplier -= 30; // Use 70% of normal time
    
    // 2. High volatility positions - use more time
    // If position is very volatile (high tau and rho), use more time
    float volatility = (res.tau * 3.0f) + (res.rho * 0.3f);
    if (volatility > 0.75f)
        multiplier += 20; // Use 120% of normal time
    
    // 3. Material imbalance - use more time for critical positions
    // If evaluation is close to zero (tactical complexity), use more time
    if (std::abs(res.eval) < 30.0f && res.rho > 0.60f)
        multiplier += 15; // Use 115% of normal time
    
    // Clamp multiplier to reasonable range
    return std::max(70, std::min(130, multiplier));
}

} // namespace HARENN

} // namespace Stockfish
