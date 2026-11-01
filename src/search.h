#pragma once

#include "types.h"
#include "position.h"
#include "movegen.h"
#include "harenn.h"
#include "dee.h"
#include "ssfzre.h"
#include "smtta.h"
#include "hamo.h"
#include "upad.h"
#include "hare.h"
#include "csfte.h"
#include "dqrs.h"
#include "aaw.h"
#include "voi_tm.h"
#include <atomic>
#include <functional>

namespace NextFish {

struct SearchLimits {
    int64_t time = -1;
    int64_t inc = 0;
    int64_t opponent_time = -1; // Opponent clock from WinBoard otim
    int moves_to_go = 0;
    int depth = 64;
    uint64_t max_nodes = 0;
    bool infinite = false;
};

struct SearchStack {
    Move current_move = MOVE_NONE;
    Move excluded_move = MOVE_NONE;
    Move killers[2] = {MOVE_NONE, MOVE_NONE};
    int path_extensions = 0;
    double branch_fractional_ext = 0.0;
    Value static_eval = 0;
    double zugzwang_risk = 0.0;
};

class SearchEngine {
public:
    SearchEngine();

    void start_search(Position& pos, const SearchLimits& limits,
                      std::function<void(int depth, Value score, int64_t time_ms, uint64_t nodes, const std::string& pv)> on_update,
                      std::function<void(Move best_move)> on_finished);

    void stop();
    bool is_stopped() const { return stopped.load(std::memory_order_relaxed); }
    void check_time();

    Value alpha_beta(Position& pos, int depth, Value alpha, Value beta, int ply,
                     bool is_pv, SearchStack* ss, Move& out_best_move);

private:
    std::atomic<bool> stopped{false};
    SearchLimits current_limits;
    uint64_t nodes_count = 0;

    Move pv_table[MAX_PLY + 4][MAX_PLY + 4];
    int pv_length[MAX_PLY + 4];
    StateInfo search_states[MAX_PLY + 4];

    Key root_pawn_key = 0;
    Key root_material_key = 0;
    Value root_second_score = -VALUE_INFINITE;
};

extern SearchEngine GlobalSearch;

} // namespace NextFish
