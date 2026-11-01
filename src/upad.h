#pragma once

#include "types.h"
#include "position.h"
#include "harenn.h"

namespace NextFish {

enum UPADNodeAction {
    NODE_CONTINUE,
    NODE_HARD_PRUNE,
    NODE_SOFT_PRUNE
};

enum UPADMoveAction {
    MOVE_SEARCH,
    MOVE_HARD_PRUNE
};

struct UPADNodeDecision {
    UPADNodeAction action = NODE_CONTINUE;
    Value return_score = 0;
};

struct UPADMoveDecision {
    UPADMoveAction action = MOVE_SEARCH;
};

class UPADEngine {
public:
    UPADEngine() = default;

    // Node-level decision (Reverse Futility Pruning, etc.)
    UPADNodeDecision make_node_decision(const Position& pos, int depth, Value beta,
                                        Value static_eval, double tau, double rho,
                                        double zugzwang_risk, bool is_pv, bool improving = true) const;

    // Move-level decision (Late Move Pruning, Futility Pruning, SEE Pruning)
    UPADMoveDecision make_move_decision(const Position& pos, Move m, int move_idx,
                                        int depth, Value alpha, Value static_eval,
                                        bool gives_check, bool is_capture, bool in_check, bool is_pv,
                                        bool improving = true, bool is_killer = false,
                                        bool attacks_king_or_queen = false) const;
};

extern UPADEngine GlobalUPAD;

} // namespace NextFish
