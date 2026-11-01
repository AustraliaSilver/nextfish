#include "voi_tm.h"
#include <cmath>
#include <algorithm>

namespace NextFish {

VOITimeManager GlobalTimeMan;

VOITimeManager::VOITimeManager() {
    start_time = std::chrono::steady_clock::now();
}

int64_t VOITimeManager::get_elapsed_ms() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
}

void VOITimeManager::init(int64_t time_remaining_ms, int64_t increment_ms, int mtg, int64_t opponent_time_ms) {
    if (mtg == 1) {
        time_rem_ms = time_remaining_ms;
    } else {
        int64_t safety = std::clamp(time_remaining_ms / 15, int64_t(30), int64_t(150));
        time_rem_ms = std::max(int64_t(5), time_remaining_ms - safety);
    }
    inc_ms = increment_ms;
    opp_time_rem_ms = opponent_time_ms;
    moves_to_go = mtg;
    panic_mode = false;
    best_move_stable_count = 0;
    prev_best_move = MOVE_NONE;
}

void VOITimeManager::initialize_move_budget(int ply, int piece_count, double tau, double rho, int legal_move_count) {
    start_time = std::chrono::steady_clock::now();

    if (legal_move_count <= 1) {
        base_alloc_ms = std::min(10.0, static_cast<double>(time_rem_ms) * 0.02);
        hard_max_ms = base_alloc_ms;
        emergency_reserve_ms = base_alloc_ms;
        return;
    }

    if (moves_to_go == 1) {
        // Fixed time per move (e.g. WinBoard "st seconds" command)
        base_alloc_ms = static_cast<double>(time_rem_ms) * 0.80;
        hard_max_ms = static_cast<double>(time_rem_ms) * 0.95;
        emergency_reserve_ms = static_cast<double>(time_rem_ms) * 0.98;
        return;
    }

    double expected_rem_moves = 0.0;
    if (moves_to_go > 0) {
        expected_rem_moves = static_cast<double>(moves_to_go);
    } else {
        double move_num = ply / 2.0;
        expected_rem_moves = std::max(16.0, 36.0 - 0.35 * move_num);
    }

    // Base allocation from remaining time pool + expected increment
    double total_virtual_pool = static_cast<double>(time_rem_ms) + expected_rem_moves * static_cast<double>(inc_ms) * 0.90;
    base_alloc_ms = (total_virtual_pool / expected_rem_moves);

    // Complexity multiplier from neural heads:
    // When tau is high (sharp tactics), invest up to 1.35x. When tau is low (quiet), scale down to 0.75x to bank time!
    double neural_mult = (0.80 + 0.35 * tau + 0.20 * rho);
    base_alloc_ms *= std::clamp(neural_mult, 0.75, 1.35);

    // Hard per-move caps:
    // In fast increment time controls (e.g. 8s+0.8s), NEVER spend > 1.08x increment unless
    // a substantial time bank (> 12 seconds) has already been accumulated!
    double cap_pct = (ply >= 32) ? 0.12 : 0.08;
    base_alloc_ms = std::min(base_alloc_ms, static_cast<double>(time_rem_ms) * cap_pct);
    hard_max_ms = std::min(static_cast<double>(time_rem_ms) * 0.25, base_alloc_ms * 1.30);
    emergency_reserve_ms = std::min(static_cast<double>(time_rem_ms) * 0.35, base_alloc_ms * 1.50);

    // Increment pacing (Clock Accumulator & Ironclad Ceiling):
    if (inc_ms > 0) {
        // Target 0.80x to 0.85x increment so we never bleed the base clock:
        double target_inc = std::min(static_cast<double>(inc_ms) * 0.84, static_cast<double>(time_rem_ms) * 0.40);
        base_alloc_ms = std::max(base_alloc_ms, target_inc);

        if (time_rem_ms > 12000) {
            // Only when substantial clock bank has accumulated (> 12s) can we surge to 1.25x inc
            double surge = (tau > 0.45 || rho > 0.35) ? static_cast<double>(inc_ms) * 1.25 : static_cast<double>(inc_ms) * 1.05;
            hard_max_ms = std::min(hard_max_ms, surge);
        } else {
            // Normal bank (<= 12s): strict ceiling at 1.06x increment (e.g. 850ms for 800ms inc).
            // Guarantees we NEVER drain our bank or enter panic mode!
            hard_max_ms = std::min(hard_max_ms, static_cast<double>(inc_ms) * 1.06);
            emergency_reserve_ms = std::min(emergency_reserve_ms, static_cast<double>(inc_ms) * 1.15);
        }
    }

    // Opponent Time Trouble Exploitation (otim):
    // If opponent clock is low (< 3500ms or < 0.55 * time_rem), shave quiet base allocations
    // so we return rapidly, bank more increment, and prevent opponent from pondering!
    if (opp_time_rem_ms > 0 && opp_time_rem_ms < 3500 && time_rem_ms > 2500) {
        if (tau < 0.35 && rho < 0.30) {
            base_alloc_ms = std::min(base_alloc_ms, std::max(180.0, static_cast<double>(inc_ms) * 0.55));
        }
    }

    if (time_rem_ms < 2000) {
        // If we have low time, but opponent is in even worse trouble, avoid premature panic:
        bool opp_in_worse_trouble = (opp_time_rem_ms > 0 && opp_time_rem_ms < time_rem_ms);
        double panic_factor = opp_in_worse_trouble ? 0.12 : 0.08;
        double panic_cap = panic_factor * static_cast<double>(time_rem_ms) + static_cast<double>(inc_ms);
        base_alloc_ms = std::min(base_alloc_ms, panic_cap * 0.80);
        hard_max_ms = std::min(hard_max_ms, panic_cap);
        emergency_reserve_ms = std::min(emergency_reserve_ms, panic_cap * 1.15);
    }

    // Safety margins: guarantee at least 80ms buffer on clock
    hard_max_ms = std::min(hard_max_ms, std::max(10.0, static_cast<double>(time_rem_ms) - 80.0));
    emergency_reserve_ms = std::min(emergency_reserve_ms, std::max(10.0, static_cast<double>(time_rem_ms) - 60.0));
}

bool VOITimeManager::should_continue_next_iteration(int completed_depth, Move best_move, Value best_score,
                                                   Value second_score, int64_t elapsed_ms, double best_move_node_frac,
                                                   double tau, double rho) {
    double active_limit = panic_mode ? emergency_reserve_ms : hard_max_ms;

    if (completed_depth < 6) {
        if (static_cast<double>(elapsed_ms) >= hard_max_ms * 0.60) return false;
        return true;
    }

    if (moves_to_go == 1) {
        // Fixed move time (st command): continue as long as below base allocation
        return static_cast<double>(elapsed_ms) < base_alloc_ms;
    }

    // Trivial mate / decisive lead
    if (completed_depth >= 7 && best_score >= VALUE_MATE_THRESHOLD - 50) {
        return false;
    }

    if (best_move == prev_best_move) {
        best_move_stable_count++;
    } else {
        best_move_stable_count = 0;
        prev_best_move = best_move;
    }

    // Fast Velocity Cutoff: Obvious or stable winning move
    // Plays in 150-250ms on clear moves (depth >= 10, gap > 180 cp) to bank time!
    if (completed_depth >= 10 && best_move_stable_count >= 3 && second_score > -VALUE_MATE_THRESHOLD
        && (best_score - second_score) > 180 && best_move_node_frac > 0.85 && rho < 0.25) {
        return false;
    }

    // Relentless Blitz Pressure on Opponent Time Trouble (otim):
    // When opponent has < 3000ms or less than 50% of our time, instantly return on stable positions
    // This gives opponent zero ponder/thinking time and adds +550ms to our clock surplus!
    if (opp_time_rem_ms > 0 && (opp_time_rem_ms < 3000 || opp_time_rem_ms < time_rem_ms * 0.50)
        && time_rem_ms > 2500 && completed_depth >= 8 && best_move_stable_count >= 2
        && second_score > -VALUE_MATE_THRESHOLD && (best_score - second_score) > 60
        && best_move_node_frac > 0.70 && tau < 0.40) {
        return false;
    }

    if (static_cast<double>(elapsed_ms) >= active_limit * 0.90) {
        return false;
    }

    // For depths below 15, prioritize reaching tactical depth if within base allocation
    if (completed_depth < 15 && static_cast<double>(elapsed_ms) < base_alloc_ms * 1.05) {
        if (static_cast<double>(elapsed_ms) * 1.20 <= active_limit) {
            return true;
        }
    }

    // Continue if we haven't reached planned base allocation
    if (static_cast<double>(elapsed_ms) < base_alloc_ms * 0.95 && completed_depth < 40) {
        return true;
    }

    // Past base allocation: check if we should extend or stop
    double predicted_next_ms = static_cast<double>(elapsed_ms) * 1.50;
    if (static_cast<double>(elapsed_ms) + predicted_next_ms * 0.40 > active_limit) {
        return false;
    }

    if (completed_depth >= 13 && static_cast<double>(elapsed_ms) >= base_alloc_ms) {
        if (best_move_stable_count >= 3 && second_score > -VALUE_MATE_THRESHOLD
            && (best_score - second_score) > 90 && rho < 0.35) {
            return false;
        }
    }

    double score_gap = (second_score <= -VALUE_MATE_THRESHOLD) ? 35.0 : std::max(0.0, static_cast<double>(best_score - second_score));
    double sigma = 20.0;
    double combined_std = std::sqrt(2.0 * sigma * sigma + 225.0);
    double z_score = -score_gap / combined_std;
    double p_rev = 0.5 * (1.0 + std::erf(z_score / std::sqrt(2.0))) * (1.0 - 0.5 * best_move_node_frac);

    double wdl_prob = 1.0 / (1.0 + std::exp(-2.5 * (best_score / 400.0)));
    double d_wdl_ds = (2.5 / 400.0) * wdl_prob * (1.0 - wdl_prob);
    double expected_delta_wdl = d_wdl_ds * (sigma * std::sqrt(2.0 / 3.141592653589793) + 7.5);
    double psi_horizon = (1.0 + 1.8 * rho * rho + 0.9 * tau);
    double evc = p_rev * expected_delta_wdl * psi_horizon * 120.0;

    double cost_ratio = predicted_next_ms / std::max(base_alloc_ms, 1.0);
    double exp_penalty = std::exp(((static_cast<double>(elapsed_ms) + predicted_next_ms) / active_limit) - 1.0);
    double toc = cost_ratio * exp_penalty * 0.40;

    return evc > toc;
}

void VOITimeManager::on_aspiration_failure(bool fail_low, Value fail_margin) {
    if (fail_low && fail_margin >= 25) {
        panic_mode = true;
        double expansion = std::min(2.0, 1.2 + static_cast<double>(fail_margin) / 100.0);
        base_alloc_ms = std::min(hard_max_ms, base_alloc_ms * expansion);
    }
}

bool VOITimeManager::is_time_up(int64_t elapsed_ms) const {
    double limit = panic_mode ? std::min(emergency_reserve_ms, hard_max_ms * 1.2) : hard_max_ms;
    double safety_cap = std::max(10.0, static_cast<double>(time_rem_ms) - 100.0);
    limit = std::min(limit, safety_cap);
    return static_cast<double>(elapsed_ms) >= limit;
}

double VOITimeManager::get_dynamic_time_pressure(int64_t elapsed_ms) const {
    double active_cap = panic_mode ? emergency_reserve_ms : hard_max_ms;
    double ratio_to_base = static_cast<double>(elapsed_ms) / std::max(base_alloc_ms, 1.0);
    double ratio_to_cap = static_cast<double>(elapsed_ms) / std::max(active_cap, 1.0);
    double pressure = 0.4 * std::clamp(ratio_to_base / 1.5, 0.0, 1.0)
                    + 0.6 * std::clamp(ratio_to_cap, 0.0, 1.0);
    return std::clamp(pressure, 0.0, 1.0);
}

} // namespace NextFish
