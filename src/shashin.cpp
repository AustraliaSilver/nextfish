/*
  Nextfish - Shashin Theory Implementation v3.2 (Final Stable)
  Highly Optimized MCTS with NNUE-based evaluations
  Copyright (C) 2004-2025 Andrea Manzo, F. Ferraguti, K.Kiniama and ShashChess developers
  Adapted for Nextfish
*/

#include "shashin.h"
#include "evaluate.h"
#include "movegen.h"
#include "bitboard.h"
#include "misc.h"
#include "search.h"
#include "thread.h"

#include <algorithm>
#include <random>
#include <limits>
#include <vector>
#include <cmath>

namespace Stockfish {

// Static members initialization
bool MoveConfig::isStrategical = false;
bool MoveConfig::isAggressive = false;
bool MoveConfig::isFortress = false;

// ==================== MCTS STACK ====================

void MCTSStack::push(Position& pos, Move m) {
    if (depth < MAX_PLY - 1) {
        moves[depth] = m;
        pos.do_move(m, states[depth]);
        depth++;
    }
}

void MCTSStack::pop(Position& pos) {
    if (depth > 0) {
        depth--;
        pos.undo_move(moves[depth]);
    }
}

void MCTSStack::clear(Position& pos) {
    while (depth > 0) pop(pos);
}

// ==================== MCTS NODE ====================

MCTSNode::MCTSNode(Move m, MCTSNode* par, double movePrior) 
    : move(m), parent(par), isExpanded(false), isTerminal(false), priorScore(movePrior) {
    visits.store(0);
    totalScore.store(0.0);
    prior.store(movePrior);
}

double MCTSNode::uctScore(double explorationConstant) const {
    int parentVisits = parent ? std::max(1, parent->visits.load()) : 1;
    int nodeVisits = visits.load();
    const double dynamicExploration = explorationConstant * (0.65 + 0.85 / (1.0 + parentVisits / 64.0));
    if (nodeVisits == 0) return priorScore + dynamicExploration * std::sqrt(std::log(parentVisits + 1));
    double exploitation = totalScore.load() / nodeVisits;
    double progressiveFactor = std::sqrt(2.0 / (1.0 + nodeVisits / 10.0));
    double exploration = dynamicExploration * progressiveFactor * std::sqrt(std::log(parentVisits + 1) / nodeVisits);
    double priorBonus = priorScore * std::max(0.05, 0.3 - nodeVisits * 0.01);
    return exploitation + exploration + priorBonus;
}

MCTSNode* MCTSNode::bestChild(double explorationConstant) const {
    MCTSNode* best = nullptr;
    double bestScore = -std::numeric_limits<double>::infinity();
    for (const auto& child : children) {
        double score = child->uctScore(explorationConstant);
        if (score > bestScore) { bestScore = score; best = child.get(); }
    }
    return best;
}

bool MCTSNode::isFullyExpanded(int legalMoveCount) const {
    return static_cast<int>(children.size()) >= legalMoveCount;
}

MCTSNode* MCTSNode::addChild(Move m, double prior) {
    auto child = std::make_unique<MCTSNode>(m, this, prior);
    MCTSNode* childPtr = child.get();
    children.push_back(std::move(child));
    return childPtr;
}

// ==================== MCTS TREE ====================

MCTSTree::MCTSTree(int iterations, double exploration, ShashinStyle treeStyle)
    : maxIterations(iterations), explorationConstant(exploration), style(treeStyle), rng(std::random_device{}()) {
    switch (style) {
        case HIGH_TAL: explorationConstant = 2.4; maxSimDepth = 6; break;
        case TAL: explorationConstant = 2.0; maxSimDepth = 5; break;
        case CAPABLANCA: explorationConstant = 1.6; maxSimDepth = 4; break;
        case PETROSIAN: explorationConstant = 1.3; maxSimDepth = 3; break;
        case HIGH_PETROSIAN: explorationConstant = 1.1; maxSimDepth = 2; break;
        default: break;
    }
}

double MCTSTree::calculateMovePrior(Position& pos, Move move) const {
    double prior = 0.5;
    if (pos.capture_stage(move)) {
        Piece captured = pos.piece_on(move.to_sq());
        if (captured != NO_PIECE) {
            int val = PieceValue[type_of(captured)] - PieceValue[type_of(pos.moved_piece(move))] / 10;
            if (val > 0) prior += 0.15 + std::min(val / 500.0, 0.2);
            else if (val >= 0) prior += 0.1;
        }
    }
    if (pos.gives_check(move)) prior += 0.12;
    if (move.type_of() == PROMOTION) prior += 0.15;
    return std::min(prior, 0.95);
}

Move MCTSTree::search(Position& rootPos, const Eval::NNUE::Networks& networks, double* outWinRate, int* outVisits,
                      int* outRootVisits, std::vector<MCTSRootStat>* outRootStats) {
    auto root = std::make_unique<MCTSNode>(Move::none(), nullptr, 1.0);
    if (MoveList<LEGAL>(rootPos).size() == 0) return Move::none();
    for (int i = 0; i < maxIterations; ++i) {
        MCTSStack stack;
        MCTSNode* selected = select(root.get(), rootPos, stack);
        MCTSNode* expanded = expandWithPrior(selected, rootPos, stack);
        double score = simulateNNUE(expanded, rootPos, networks, stack);
        backpropagate(expanded, score);
        stack.clear(rootPos);
    }
    collectRootStats(root.get(), outRootStats);
    return getBestMove(root.get(), outWinRate, outVisits, outRootVisits);
}

MCTSNode* MCTSTree::select(MCTSNode* node, Position& rootPos, MCTSStack& stack) {
    std::vector<Move> path = getPathFromRoot(node);
    for (Move m : path) if (m != Move::none()) stack.push(rootPos, m);
    while (!node->isTerminal && !node->children.empty()) {
        if (MoveList<LEGAL>(rootPos).size() == 0) { node->isTerminal = true; break; }
        const int nodeVisits = std::max(1, node->visits.load());
        const int allowed = std::max(1, int(1.8 * std::sqrt(double(nodeVisits))));
        if (static_cast<int>(node->children.size()) < allowed) break;
        node = node->bestChild(explorationConstant);
        if (node && node->move != Move::none()) stack.push(rootPos, node->move);
        else break;
    }
    return node;
}

MCTSNode* MCTSTree::expandWithPrior(MCTSNode* node, Position& rootPos, MCTSStack& stack) {
    if (node->isTerminal) return node;
    MoveList<LEGAL> legals(rootPos);
    if (legals.size() == 0) { node->isTerminal = true; return node; }
    Move bestM = Move::none(); double bestP = -1.0;
    for (const auto& m : legals) {
        bool found = false; for (const auto& c : node->children) if (c->move == m) { found = true; break; }
        if (!found) { double p = calculateMovePrior(rootPos, m); if (p > bestP) { bestP = p; bestM = m; } }
    }
    if (bestM != Move::none()) {
        MCTSNode* child = node->addChild(bestM, bestP);
        stack.push(rootPos, bestM);
        child->isTerminal = (MoveList<LEGAL>(rootPos).size() == 0);
        stack.pop(rootPos);
        return child;
    }
    return node;
}

double MCTSTree::simulateNNUE(MCTSNode* node, Position& rootPos, const Eval::NNUE::Networks& networks, MCTSStack& stack) {
    (void)node; (void)stack;
    return evaluateNNUE(rootPos, rootPos.side_to_move(), networks);
}

double MCTSTree::evaluateNNUE(const Position& pos, Color rootSide, const Eval::NNUE::Networks& networks) const {
    Eval::NNUE::AccumulatorStack acc; Eval::NNUE::AccumulatorCaches caches(networks);
    Value v = Eval::evaluate(networks, pos, acc, caches, 0);
    double s = 1.0 / (1.0 + std::exp(-v / 400.0));
    return (pos.side_to_move() == rootSide) ? s : 1.0 - s;
}

void MCTSTree::backpropagate(MCTSNode* node, double score) {
    while (node != nullptr) {
        node->visits.fetch_add(1, std::memory_order_relaxed);
        double expected = node->totalScore.load(std::memory_order_relaxed);
        double desired;
        do { desired = expected + score; }
        while (!node->totalScore.compare_exchange_weak(expected, desired, std::memory_order_relaxed));
        score = 1.0 - score;
        node = node->parent;
    }
}

Move MCTSTree::getBestMove(MCTSNode* root, double* outWinRate, int* outVisits, int* outRootVisits) const {
    if (outWinRate) *outWinRate = 0.5; if (outVisits) *outVisits = 0; if (outRootVisits) *outRootVisits = root ? root->visits.load() : 0;
    if (!root || root->children.empty()) return Move::none();
    MCTSNode* best = nullptr; double bestS = -std::numeric_limits<double>::infinity();
    for (const auto& c : root->children) {
        int v = c->visits.load(); if (v == 0) continue;
        double wr = c->totalScore.load() / v;
        double robust = wr + 0.15 * (std::sqrt(v) / 10.0);
        if (v > maxIterations / 5) robust += 0.05;
        if (robust > bestS) { bestS = robust; best = c.get(); }
    }
    if (best && outVisits) *outVisits = best->visits.load();
    if (best && outWinRate) *outWinRate = best->totalScore.load() / best->visits.load();
    return best ? best->move : Move::none();
}

std::vector<Move> MCTSTree::getPathFromRoot(MCTSNode* node) const {
    std::vector<Move> p; MCTSNode* c = node;
    while (c && c->parent) { p.push_back(c->move); c = c->parent; }
    std::reverse(p.begin(), p.end()); return p;
}

void MCTSTree::collectRootStats(MCTSNode* root, std::vector<MCTSRootStat>* outRootStats) const {
    if (!outRootStats) return;
    outRootStats->clear();
    if (!root) return;
    outRootStats->reserve(root->children.size());
    for (const auto& child : root->children) {
        const int visits = child->visits.load();
        if (visits <= 0) continue;
        MCTSRootStat stat;
        stat.move = child->move;
        stat.visits = visits;
        stat.winRate = child->totalScore.load() / visits;
        outRootStats->push_back(stat);
    }
    std::sort(outRootStats->begin(), outRootStats->end(),
              [](const MCTSRootStat& a, const MCTSRootStat& b) { return a.visits > b.visits; });
}

// ==================== SHASHIN MANAGER ====================

ShashinManager::ShashinManager() = default;
ShashinManager::~ShashinManager() = default;

void ShashinManager::setStaticState(const Position& pos) {
    state.staticState.stmKingExposed = detectKingExposed(pos, pos.side_to_move());
    state.staticState.opponentKingExposed = detectKingExposed(pos, ~pos.side_to_move());
    state.staticState.isSacrificial = detectSacrificial(pos);
    state.staticState.kingDanger = detectKingDanger(pos);
    state.staticState.pawnsNearPromotion = detectPawnsNearPromotion(pos);
    state.staticState.allPiecesCount = pos.count<ALL_PIECES>() > 18;
    state.staticState.legalMoveCount = uint8_t(MoveList<LEGAL>(pos).size());
    state.staticState.highMaterial = state.staticState.allPiecesCount;
    updateDynamicState(pos);
    currentStyle = classifyPosition(pos);
}

void ShashinManager::updateDynamicState(const Position& pos) {
    const auto& s = state.staticState; auto& d = state.dynamicDerived;
    d.isStrategical = !s.stmKingExposed && !s.opponentKingExposed && !s.isSacrificial && !s.kingDanger;
    d.isAggressive = s.stmKingExposed || s.opponentKingExposed || s.kingDanger || s.isSacrificial;
    d.isTactical = s.kingDanger || s.isSacrificial || s.pawnsNearPromotion;
    d.isHighTal = s.stmKingExposed && s.opponentKingExposed && s.kingDanger;
    d.isComplex = pos.count<ALL_PIECES>() > 14 && !d.isStrategical && !d.isAggressive;
    d.isMCTSApplicable = (d.isHighTal || (d.isAggressive && d.isTactical)) && s.legalMoveCount > 14;
}

void ShashinManager::updateRootShashinState(Value score, const Position& pos, Depth depth, Depth rootDepth) {
    // Tournament-safe mode: keep Shashin classification available for diagnostics,
    // but disable direct search-parameter overrides that caused Elo regressions.
    (void) score;
    (void) depth;
    (void) rootDepth;
    updateDynamicState(pos);
    MoveConfig::isStrategical = false;
    MoveConfig::isAggressive = false;
    MoveConfig::isFortress = false;
}

ShashinStyle ShashinManager::classifyPosition(const Position& pos) const {
    const auto& d = state.dynamicDerived;
    if (d.isHighTal) return HIGH_TAL;
    if (d.isAggressive && !d.isStrategical) return TAL;
    if (d.isStrategical && d.isAggressive) return CAPABLANCA;
    if (d.isStrategical && !d.isAggressive) return PETROSIAN;
    if (isFortress(pos)) return HIGH_PETROSIAN;
    return UNKNOWN_STYLE;
}

void ShashinManager::updateCurrentStyle(const Position& pos) { currentStyle = classifyPosition(pos); }
bool ShashinManager::isStrategical() const { return state.dynamicDerived.isStrategical; }
bool ShashinManager::isAggressive() const { return state.dynamicDerived.isAggressive; }
bool ShashinManager::isTal() const { return isAggressive() && !isStrategical(); }
bool ShashinManager::isPetrosian() const { return isStrategical() && !isAggressive(); }
bool ShashinManager::isCapablanca() const { return isStrategical() && isAggressive(); }
bool ShashinManager::isTactical() const { return state.dynamicDerived.isTactical; }
bool ShashinManager::isComplexPosition() const { return state.dynamicDerived.isComplex; }

bool ShashinManager::isFortress(const Position& pos) const {
    if (pos.count<ALL_PIECES>() > 12) return false;
    Bitboard p = pos.pieces(PAWN);
    Bitboard b = (shift<NORTH>(p) | shift<SOUTH>(p)) & pos.pieces();
    return popcount(b) >= 4 || (pos.count<BISHOP>() >= 2 && pos.count<PAWN>() <= 4);
}

bool ShashinManager::isMCTSApplicableByValue() const { return state.dynamicDerived.isMCTSApplicable; }
bool ShashinManager::isMCTSExplorationApplicable() const { return isComplexPosition() || (state.staticState.highMaterial && isCapablanca()); }
const char* ShashinManager::getStyleName() const {
    switch (currentStyle) {
        case HIGH_TAL: return "High Tal"; case TAL: return "Tal"; case CAPABLANCA: return "Capablanca";
        case PETROSIAN: return "Petrosian"; case HIGH_PETROSIAN: return "High Petrosian"; default: return "Balanced";
    }
}
const char* ShashinManager::getStyleEmoji() const {
    switch (currentStyle) {
        case HIGH_TAL: return "[FIRE]"; case TAL: return "[SWORD]"; case CAPABLANCA: return "[SCALE]";
        case PETROSIAN: return "[SHIELD]"; case HIGH_PETROSIAN: return "[CASTLE]"; default: return "[SCALE]";
    }
}
bool ShashinManager::avoidStep10() const { return isStrategical() && state.staticState.kingDanger; }
bool ShashinManager::allowCrystalProbCut() const { return isTal() || isComplexPosition(); }
bool ShashinManager::useStep17CrystalLogic() const { return isTal() || (isComplexPosition() && state.staticState.kingDanger); }
Value ShashinManager::static_value(const Position& pos) { (void)pos; return VALUE_NONE; }

Move ShashinManager::runMCTSSearch(Position& pos, const Eval::NNUE::Networks& networks, int iterations, 
                                   double* outWinRate, int* outVisits, int* outRootVisits,
                                   std::vector<MCTSRootStat>* outRootStats) {
    if (!config.useMCTS || !isMCTSApplicableByValue()) return Move::none();
    MCTSTree tree(std::clamp(iterations, 1, 300), config.mctsExploration, currentStyle);
    return tree.search(pos, networks, outWinRate, outVisits, outRootVisits, outRootStats);
}

void ShashinManager::syncMCTSOptions(bool enabled, int iterations) {
    config.useMCTS = enabled; config.mctsIterations = std::max(1, iterations);
}

bool ShashinManager::detectKingExposed(const Position& pos, Color side) const {
    Square k = pos.square<KING>(side); Bitboard p = 0;
    if (side == WHITE) { if (rank_of(k) >= RANK_2 && rank_of(k) <= RANK_4) p = pos.pieces(side, PAWN) & (shift<SOUTH>(k) | shift<SOUTH_WEST>(k) | shift<SOUTH_EAST>(k)); }
    else { if (rank_of(k) >= RANK_5 && rank_of(k) <= RANK_7) p = pos.pieces(side, PAWN) & (shift<NORTH>(k) | shift<NORTH_WEST>(k) | shift<NORTH_EAST>(k)); }
    if (!p) return true;
    File f = file_of(k); Bitboard m = file_bb(f); if (f > FILE_A) m |= file_bb(File(f - 1)); if (f < FILE_H) m |= file_bb(File(f + 1));
    return !(pos.pieces(PAWN) & m);
}

bool ShashinManager::detectSacrificial(const Position& pos) const {
    Color us = pos.side_to_move(); Square ek = pos.square<KING>(~us); Bitboard az = attacks_bb<KING>(ek);
    if (popcount(az & pos.pieces(us)) >= 2) return true;
    Bitboard oq = pos.pieces(us, QUEEN); return oq && distance(lsb(oq), ek) <= 3;
}

bool ShashinManager::detectKingDanger(const Position& pos) const {
    Color us = pos.side_to_move(); Square ok = pos.square<KING>(us); Bitboard kz = attacks_bb<KING>(ok);
    int a = 0; for (PieceType pt = KNIGHT; pt <= QUEEN; ++pt) { Bitboard ps = pos.pieces(~us, pt); while (ps) if (attacks_bb(pt, pop_lsb(ps), pos.pieces()) & kz) a++; }
    return a >= 2;
}

bool ShashinManager::detectPawnsNearPromotion(const Position& pos) const {
    return (pos.pieces(WHITE, PAWN) & (Rank6BB | Rank7BB)) || (pos.pieces(BLACK, PAWN) & (Rank2BB | Rank3BB));
}

int ShashinManager::calculateActivity(const Position& pos) const {
    int a = 0; for (PieceType pt = KNIGHT; pt <= QUEEN; ++pt) a += popcount(pos.pieces(pt)) * 10;
    return a;
}

} // namespace Stockfish
