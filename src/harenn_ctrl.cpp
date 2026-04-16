#include "harenn_ctrl.h"
#include "harenn.h"
#include "bitboard.h"
#include "position.h"
#include "dee.h"
#include <algorithm>

namespace Stockfish {

namespace HARENN {

namespace {
    struct NodeCache {
        Key key = 0;
        TacticalProfile profile;
        Bitboard usHanging = 0;
    };
    thread_local NodeCache nodeCache;
}

void Controller::init() {
    GuidanceProvider::init();
}

TacticalProfile Controller::analyze(const Position& pos) {
    if (pos.key() == nodeCache.key)
        return nodeCache.profile;

    TacticalProfile profile;
    Color us = pos.side_to_move();
    Color them = ~us;
    Square ksq = pos.square<KING>(us);

    Bitboard usAttacks, themAttacks;
    DEE::Evaluator::compute_both_attack_maps(pos, usAttacks, themAttacks);
    
    profile.attack_density = popcount(usAttacks | themAttacks);
    profile.king_ring_pressure = popcount(themAttacks & attacks_bb<KING>(ksq));

    nodeCache.usHanging = pos.pieces(us) & themAttacks & ~usAttacks;
    profile.hanging_pieces_count = popcount(nodeCache.usHanging);

    EvalResult res = GuidanceProvider::query(pos);
    profile.nn_tau = res.tau;

    nodeCache.key = pos.key();
    nodeCache.profile = profile;
    return profile;
}

int Controller::get_smart_reduction(const Position& pos, Depth depth, Move m, int moveCount, int baseR) {
    (void)moveCount;
    if (!m || depth < 6) return baseR;
    
    TacticalProfile profile = analyze(pos);
    if (profile.nn_tau > 0.85f && pos.capture_stage(m))
        return std::max(0, baseR - 128);

    return baseR;
}

int Controller::get_move_bonus(const Position& pos, Move m) {
    analyze(pos); 
    int bonus = 0;

    if (nodeCache.usHanging & square_bb(m.from_sq()))
        bonus += 40;

    if (distance(m.to_sq(), pos.square<KING>(~pos.side_to_move())) <= 1)
        bonus += 20;

    if (nodeCache.profile.nn_tau > 0.6f)
        bonus += 10;

    return bonus;
}

int Controller::adjust_aspiration(const Position& pos, int delta) {
    (void)pos;
    return delta;
}

} // namespace HARENN

} // namespace Stockfish
