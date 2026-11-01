#pragma once

#include "types.h"
#include <cstdint>
#include <chrono>

namespace NextFish {

class VOITimeManager {
public:
    VOITimeManager();

    void init(int64_t time_remaining_ms, int64_t increment_ms, int moves_to_go = 0, int64_t opponent_time_ms = -1);
    void initialize_move_budget(int ply, int piece_count, double tau, double rho, int legal_move_count);

    bool should_continue_next_iteration(int completed_depth, Move best_move, Value best_score,
                                        Value second_score, int64_t elapsed_ms, double best_move_node_frac,
                                        double tau, double rho);

    void on_aspiration_failure(bool fail_low, Value fail_margin);
    bool is_time_up(int64_t elapsed_ms) const;
    double get_dynamic_time_pressure(int64_t elapsed_ms) const;

    int64_t get_elapsed_ms() const;
    int64_t get_opponent_time_ms() const { return opp_time_rem_ms; }

private:
    std::chrono::time_point<std::chrono::steady_clock> start_time;

    int64_t time_rem_ms = 0;
    int64_t inc_ms = 0;
    int64_t opp_time_rem_ms = -1;
    int moves_to_go = 0;

    double base_alloc_ms = 100.0;
    double hard_max_ms = 500.0;
    double emergency_reserve_ms = 800.0;
    bool panic_mode = false;

    Move prev_best_move = MOVE_NONE;
    int best_move_stable_count = 0;
};

extern VOITimeManager GlobalTimeMan;

} // namespace NextFish
