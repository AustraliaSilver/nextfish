#pragma once

#include "types.h"
#include <cstdint>
#include <chrono>

namespace NextFish {

struct OpponentTimeProfile {
    int64_t last_otim = -1;
    double theta_scale = 1.0;
    double theta_inc = 0.85;
    double theta_tau = 0.65;
    int samples = 0;

    void record_opponent_move(int64_t new_otim, int64_t inc_ms, double prev_tau) {
        if (last_otim > 0 && inc_ms > 0) {
            int64_t spent = (last_otim - new_otim) + inc_ms;
            if (spent >= 0 && spent < 30000) {
                double spent_ratio = static_cast<double>(spent) / std::max(1.0, static_cast<double>(inc_ms));
                // Online adaptation via Recursive Least Squares / Exponential update
                double expected_ratio = theta_scale * theta_inc * (1.0 + theta_tau * (prev_tau - 0.3));
                double error = spent_ratio - expected_ratio;
                theta_inc = std::clamp(theta_inc + 0.15 * error, 0.40, 1.60);
                samples++;
            }
        }
        last_otim = new_otim;
    }

    int64_t predict_opponent_spent_ms(int64_t current_otim, int64_t inc_ms, double tau, int legal_moves, bool is_check) const {
        if (current_otim <= 0) return inc_ms;
        if (legal_moves == 1) return 20; // Forced move: opponent moves in ~20ms
        if (is_check && legal_moves <= 2) return 120; // Evasion: fast reaction
        
        double base = (inc_ms > 0) ? (inc_ms * theta_inc) : (current_otim / 30.0);
        double complexity_mult = 1.0 + theta_tau * std::clamp(tau - 0.25, -0.2, 0.8);
        double predicted = base * complexity_mult;
        return static_cast<int64_t>(std::clamp(predicted, 50.0, static_cast<double>(current_otim) * 0.45));
    }
};

class VOITimeManager {
public:
    VOITimeManager();

    void init(int64_t time_remaining_ms, int64_t increment_ms, int moves_to_go = 0, int64_t opponent_time_ms = -1);
    void initialize_move_budget(int ply, int piece_count, double tau, double rho, int legal_move_count, int opponent_replies = 20, bool check_given = false);

    bool should_continue_next_iteration(int completed_depth, Move best_move, Value best_score,
                                        Value second_score, int64_t elapsed_ms, double best_move_node_frac,
                                        double tau, double rho);

    void on_aspiration_failure(bool fail_low, Value fail_margin);
    bool is_time_up(int64_t elapsed_ms) const;
    double get_dynamic_time_pressure(int64_t elapsed_ms) const;

    int64_t get_elapsed_ms() const;
    int64_t get_opponent_time_ms() const { return opp_time_rem_ms; }
    const OpponentTimeProfile& get_opp_profile() const { return opp_profile; }

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

    OpponentTimeProfile opp_profile;
    int64_t predicted_opp_time_ms = 800;
};

extern VOITimeManager GlobalTimeMan;

} // namespace NextFish
