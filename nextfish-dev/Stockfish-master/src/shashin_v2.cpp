/*
  Nextfish - Shashin Theory Implementation v2
  Enhanced MCTS with better simulation and evaluation
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
#include <thread>
#include <future>
#include <vector>
#include <mutex>

namespace Stockfish {

// Static members initialization
bool MoveConfig::isStrategical = false;
bool MoveConfig::isAggressive = false;
bool MoveConfig::isFortress = false;

// ==================== ENHANCED MCTS IMPLEMENTATION ====================

MCTSNode::MCTSNode(Move m, MCTSNode* par, double movePrior) 
    : move(m), parent(par), isExpanded(false), isTerminal(false), priorScore(movePrior) {
    visits.store(0);
    totalScore.store(0.0);
    prior.store(movePrior);
}

double MCTSNode::uctScore(double explorationConstant) const {
    int parentVisits = parent ? parent->visits.load() : 1;
    if (visits.load() == 0) {
        // Use prior score for unvisited nodes (PUCT formula)
        return priorScore + explorationConstant * std::sqrt(std::log(parentVisits));
    }
    double exploitation = totalScore.load() / visits.load();
    double exploration = explorationConstant * std::sqrt(std::log(parentVisits) / visits.load());
    // PUCT: combine prior knowledge with UCT
    return exploitation + exploration + (priorScore * 0.1);
}

MCTSNode* MCTSNode::bestChild(double explorationConstant) const {
    MCTSNode* best = nullptr;
    double bestScore = -std::numeric_limits<double>::infinity();
    
    for (const auto& child : children) {
        double score = child->uctScore(explorationConstant);
        if (score > bestScore) {
            bestScore = score;
            best = child.get();
        }
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

// ==================== ENHANCED MCTSTree ====================

MCTSTree::MCTSTree(int iterations, double exploration, ShashinStyle style,
                   NNUE::Network* net, NNUE::AccumulatorStack* acc)
    : maxIterations(iterations), 
      explorationConstant(exploration), 
      style(style),
      network(net),
      accumulatorStack(acc),
      rng(std::random_device{}()) {
    
    // Adjust exploration based on style
    switch (style) {
        case HIGH_TAL: explorationConstant = 2.0; maxSimDepth = 50; break;
        case TAL: explorationConstant = 1.8; maxSimDepth = 45; break;
        case CAPABLANCA: explorationConstant = 1.414; maxSimDepth = 40; break;
        case PETROSIAN: explorationConstant = 1.2; maxSimDepth = 35; break;
        case HIGH_PETROSIAN: explorationConstant = 1.0; maxSimDepth = 30; break;
        default: explorationConstant = exploration; maxSimDepth = 40; break;
    }
}

struct StateMove {
    StateInfo state;
    Move move;
};

// Calculate move prior based on history and heuristics
double MCTSTree::calculateMovePrior(Position& pos, Move move) const {
    double prior = 0.5; // Base prior
    
    // Capture bonus
    if (pos.capture_stage(move)) {
        prior += 0.2;
        Piece captured = pos.piece_on(move.to_sq());
        if (captured != NO_PIECE) {
            prior += PieceValue[type_of(captured)] / 3000.0;
        }
    }
    
    // Check bonus
    if (pos.gives_check(move)) {
        prior += 0.15;
    }
    
    // Promotion bonus
    if (move.type_of() == PROMOTION) {
        prior += 0.25;
    }
    
    // History heuristic (if available)
    // This would need access to history tables from search
    
    return std::min(prior, 0.95); // Cap at 0.95
}

Move MCTSTree::search(Position& rootPos) {
    auto root = std::make_unique<MCTSNode>(Move::none(), nullptr, 1.0);
    MoveList<LEGAL> rootMoves(rootPos);
    
    if (rootMoves.size() == 0) {
        return Move::none();
    }
    
    // Pre-calculate priors for all root moves
    std::vector<std::pair<Move, double>> movePriors;
    for (const auto& move : rootMoves) {
        movePriors.push_back({move, calculateMovePrior(rootPos, move)});
    }
    
    // Single-threaded MCTS for now (can be parallelized)
    for (int i = 0; i < maxIterations; ++i) {
        // Selection
        MCTSNode* selected = select(root.get(), rootPos);
        
        // Expansion with prior
        MCTSNode* expanded = expandWithPrior(selected, rootPos, movePriors);
        
        // Enhanced simulation
        double score = simulateEnhanced(expanded, rootPos);
        
        // Backpropagation
        backpropagate(expanded, score);
    }
    
    return getBestMove(root.get());
}

MCTSNode* MCTSTree::select(MCTSNode* node, Position& rootPos) {
    std::vector<Move> path = getPathFromRoot(node);
    std::vector<StateMove> stateStack;
    stateStack.reserve(path.size());
    
    for (Move m : path) {
        if (m != Move::none()) {
            stateStack.emplace_back();
            stateStack.back().move = m;
            rootPos.do_move(m, stateStack.back().state);
        }
    }
    
    while (!node->isTerminal && !node->children.empty()) {
        MoveList<LEGAL> legalMoves(rootPos);
        if (node->isFullyExpanded(static_cast<int>(legalMoves.size()))) {
            node = node->bestChild(explorationConstant);
            if (node && node->move != Move::none()) {
                stateStack.emplace_back();
                stateStack.back().move = node->move;
                rootPos.do_move(node->move, stateStack.back().state);
            }
        } else {
            break;
        }
    }
    
    // Undo all moves
    for (int i = static_cast<int>(stateStack.size()) - 1; i >= 0; --i) {
        rootPos.undo_move(stateStack[i].move);
    }
    return node;
}

MCTSNode* MCTSTree::expandWithPrior(MCTSNode* node, Position& rootPos, 
                                    const std::vector<std::pair<Move, double>>& movePriors) {
    if (node->isTerminal) {
        return node;
    }
    
    std::vector<Move> path = getPathFromRoot(node);
    std::vector<StateMove> stateStack;
    stateStack.reserve(path.size() + 1);
    
    for (Move m : path) {
        if (m != Move::none()) {
            stateStack.emplace_back();
            stateStack.back().move = m;
            rootPos.do_move(m, stateStack.back().state);
        }
    }
    
    MoveList<LEGAL> legalMoves(rootPos);
    if (legalMoves.size() == 0) {
        node->isTerminal = true;
        for (int i = static_cast<int>(stateStack.size()) - 1; i >= 0; --i) {
            rootPos.undo_move(stateStack[i].move);
        }
        return node;
    }
    
    // Find unexpanded moves
    std::vector<Move> existingMoves;
    for (const auto& child : node->children) {
        existingMoves.push_back(child->move);
    }
    
    // Find first unexpanded move with best prior
    Move bestUnexpanded = Move::none();
    double bestPrior = -1.0;
    
    for (const auto& move : legalMoves) {
        if (std::find(existingMoves.begin(), existingMoves.end(), move) == existingMoves.end()) {
            // Find prior for this move
            double prior = 0.5;
            for (const auto& mp : movePriors) {
                if (mp.first == move) {
                    prior = mp.second;
                    break;
                }
            }
            if (prior > bestPrior) {
                bestPrior = prior;
                bestUnexpanded = move;
            }
        }
    }
    
    if (bestUnexpanded != Move::none()) {
        MCTSNode* child = node->addChild(bestUnexpanded, bestPrior);
        StateInfo st;
        rootPos.do_move(bestUnexpanded, st);
        MoveList<LEGAL> childMoves(rootPos);
        child->isTerminal = (childMoves.size() == 0);
        rootPos.undo_move(bestUnexpanded);
        
        for (int i = static_cast<int>(stateStack.size()) - 1; i >= 0; --i) {
            rootPos.undo_move(stateStack[i].move);
        }
        return child;
    }
    
    for (int i = static_cast<int>(stateStack.size()) - 1; i >= 0; --i) {
        rootPos.undo_move(stateStack[i].move);
    }
    return node;
}

// Enhanced simulation with quiescence-like search
double MCTSTree::simulateEnhanced(MCTSNode* node, Position& rootPos) {
    std::vector<Move> path = getPathFromRoot(node);
    std::vector<StateMove> stateStack;
    stateStack.reserve(maxSimDepth + path.size());
    
    for (Move m : path) {
        if (m != Move::none()) {
            stateStack.emplace_back();
            stateStack.back().move = m;
            rootPos.do_move(m, stateStack.back().state);
        }
    }
    
    int depth = 0;
    Color rootSide = rootPos.side_to_move();
    bool inQuiescence = false;
    
    while (depth < maxSimDepth) {
        MoveList<LEGAL> legalMoves(rootPos);
        
        // Check for terminal
        if (legalMoves.size() == 0) {
            double result;
            if (rootPos.checkers()) {
                result = (rootPos.side_to_move() == rootSide) ? 0.0 : 1.0;
            } else {
                result = 0.5; // Stalemate
            }
            for (int i = static_cast<int>(stateStack.size()) - 1; i >= 0; --i) {
                rootPos.undo_move(stateStack[i].move);
            }
            return result;
        }
        
        // Enter quiescence after 20 plies or when no captures
        if (depth >= 20 || inQuiescence) {
            inQuiescence = true;
            // In quiescence, only consider captures and checks
            Move bestMove = Move::none();
            int bestSee = -999999;
            
            for (const auto& m : legalMoves) {
                if (rootPos.capture_stage(m) || rootPos.gives_check(m)) {
                    int see = rootPos.see(m);
                    if (see > bestSee) {
                        bestSee = see;
                        bestMove = m;
                    }
                }
            }
            
            if (bestMove != Move::none()) {
                stateStack.emplace_back();
                stateStack.back().move = bestMove;
                rootPos.do_move(bestMove, stateStack.back().state);
                depth++;
                continue;
            } else {
                // No more forcing moves, evaluate
                break;
            }
        }
        
        // Regular playout - use weighted random selection
        std::vector<std::pair<Move, double>> weightedMoves;
        double totalWeight = 0.0;
        
        for (const auto& m : legalMoves) {
            double weight = 1.0;
            
            // Prefer captures
            if (rootPos.capture_stage(m)) {
                Piece captured = rootPos.piece_on(m.to_sq());
                weight += PieceValue[type_of(captured)] / 100.0;
            }
            
            // Prefer checks
            if (rootPos.gives_check(m)) {
                weight += 2.0;
            }
            
            // Prefer promotions
            if (m.type_of() == PROMOTION) {
                weight += 5.0;
            }
            
            weightedMoves.push_back({m, weight});
            totalWeight += weight;
        }
        
        // Weighted random selection
        std::uniform_real_distribution<> dis(0.0, totalWeight);
        double randomValue = dis(rng);
        double cumulative = 0.0;
        Move selectedMove = Move::none();
        
        for (const auto& [m, w] : weightedMoves) {
            cumulative += w;
            if (cumulative >= randomValue) {
                selectedMove = m;
                break;
            }
        }
        
        if (selectedMove == Move::none()) {
            selectedMove = weightedMoves.back().first;
        }
        
        stateStack.emplace_back();
        stateStack.back().move = selectedMove;
        rootPos.do_move(selectedMove, stateStack.back().state);
        depth++;
    }
    
    // Enhanced evaluation
    double score = evaluateEnhanced(rootPos, rootSide);
    
    for (int i = static_cast<int>(stateStack.size()) - 1; i >= 0; --i) {
        rootPos.undo_move(stateStack[i].move);
    }
    return score;
}

void MCTSTree::backpropagate(MCTSNode* node, double score) {
    while (node != nullptr) {
        node->visits.fetch_add(1, std::memory_order_relaxed);
        
        // Use atomic add for thread safety
        double expected = node->totalScore.load(std::memory_order_relaxed);
        double desired;
        do {
            desired = expected + score;
        } while (!node->totalScore.compare_exchange_weak(
            expected, desired, 
            std::memory_order_relaxed,
            std::memory_order_relaxed));
        
        score = 1.0 - score;
        node = node->parent;
    }
}

Move MCTSTree::getBestMove(MCTSNode* root) const {
    if (!root || root->children.empty()) {
        return Move::none();
    }
    
    MCTSNode* bestChild = nullptr;
    double bestScore = -std::numeric_limits<double>::infinity();
    
    for (const auto& child : root->children) {
        int visits = child->visits.load();
        double score = child->totalScore.load() / std::max(visits, 1);
        
        // Combine win rate with visit count (robust child selection)
        double robustScore = score + 0.1 * std::sqrt(visits);
        
        if (robustScore > bestScore) {
            bestScore = robustScore;
            bestChild = child.get();
        }
    }
    
    return bestChild ? bestChild->move : Move::none();
}

std::vector<Move> MCTSTree::getPathFromRoot(MCTSNode* node) const {
    std::vector<Move> path;
    MCTSNode* current = node;
    while (current != nullptr && current->parent != nullptr) {
        path.push_back(current->move);
        current = current->parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// Enhanced evaluation with NNUE if available, else advanced heuristics
double MCTSTree::evaluateEnhanced(const Position& pos, Color rootSide) const {
    Value evalValue = VALUE_NONE;
    
    // Try to use NNUE evaluation if network is available
    if (network && accumulatorStack) {
        try {
            // Note: This requires proper NNUE setup which may not be available
            // evalValue = Eval::evaluate(*network, pos, *accumulatorStack, ...);
        } catch (...) {
            // Fall back to heuristic evaluation
        }
    }
    
    // Heuristic evaluation
    if (evalValue == VALUE_NONE) {
        evalValue = heuristicEvaluate(pos);
    }
    
    // Normalize to [0, 1]
    double score = 0.5 + evalValue / 8000.0;
    score = std::max(0.0, std::min(1.0, score));
    
    Color us = pos.side_to_move();
    if (us != rootSide) {
        score = 1.0 - score;
    }
    
    return score;
}

// Advanced heuristic evaluation
Value MCTSTree::heuristicEvaluate(const Position& pos) const {
    int material[COLOR_NB] = {0, 0};
    int mobility[COLOR_NB] = {0, 0};
    int kingSafety[COLOR_NB] = {0, 0};
    int threats[COLOR_NB] = {0, 0};
    
    // Material and piece-square evaluation
    for (PieceType pt = PAWN; pt <= QUEEN; ++pt) {
        for (Color c : {WHITE, BLACK}) {
            Bitboard pieces = pos.pieces(c, pt);
            int count = popcount(pieces);
            material[c] += count * PieceValue[pt];
            
            // Mobility for non-pawn pieces
            if (pt != PAWN && pt != KING) {
                Bitboard attacks = 0;
                Bitboard bb = pieces;
                while (bb) {
                    Square sq = pop_lsb(bb);
                    attacks |= attacks_bb(pt, sq, pos.pieces());
                }
                int mob = popcount(attacks & ~pos.pieces(c));
                mobility[c] += mob * (pt == KNIGHT ? 3 : pt == BISHOP ? 3 : pt == ROOK ? 2 : 4);
            }
        }
    }
    
    // King safety evaluation
    for (Color c : {WHITE, BLACK}) {
        Square kingSq = pos.square<KING>(c);
        
        // Pawn shield
        Bitboard pawns = pos.pieces(c, PAWN);
        Bitboard shieldZone = attacks_bb<KING>(kingSq);
        int shieldPawns = popcount(shieldZone & pawns);
        kingSafety[c] += shieldPawns * 25;
        
        // King exposure penalty
        File kf = file_of(kingSq);
        if (kf >= FILE_C && kf <= FILE_F) {
            // King in center = bad
            kingSafety[c] -= 50;
        }
        
        // Open files near king
        Bitboard fileMask = file_bb(kf);
        if (kf > FILE_A) fileMask |= file_bb(File(kf - 1));
        if (kf < FILE_H) fileMask |= file_bb(File(kf + 1));
        if (popcount(pawns & fileMask) == 0) {
            kingSafety[c] -= 40; // Open file
        }
        
        // Attacked squares near king
        Bitboard kingArea = attacks_bb<KING>(kingSq);
        kingArea |= shift<NORTH>(kingArea) | shift<SOUTH>(kingArea);
        Color them = ~c;
        int attackers = 0;
        for (PieceType pt = KNIGHT; pt <= QUEEN; ++pt) {
            Bitboard theirPieces = pos.pieces(them, pt);
            while (theirPieces) {
                Square sq = pop_lsb(theirPieces);
                if (attacks_bb(pt, sq, pos.pieces()) & kingArea) {
                    attackers++;
                }
            }
        }
        kingSafety[c] -= attackers * 15;
    }
    
    // Threat detection
    for (Color c : {WHITE, BLACK}) {
        Color them = ~c;
        Bitboard ourAttacks = 0;
        for (PieceType pt = KNIGHT; pt <= QUEEN; ++pt) {
            Bitboard pieces = pos.pieces(c, pt);
            while (pieces) {
                Square sq = pop_lsb(pieces);
                ourAttacks |= attacks_bb(pt, sq, pos.pieces());
            }
        }
        
        // Hanging pieces
        Bitboard theirPieces = pos.pieces(them);
        Bitboard hanging = theirPieces & ourAttacks & ~pos.attacks_from(them);
        threats[c] += popcount(hanging) * 50;
        
        // Passed pawns
        Bitboard ourPawns = pos.pieces(c, PAWN);
        Bitboard theirPawns = pos.pieces(them, PAWN);
        Bitboard passed = ourPawns;
        if (c == WHITE) {
            passed &= ~(shift<SOUTH>(theirPawns) | shift<SOUTH_WEST>(theirPawns) | shift<SOUTH_EAST>(theirPawns));
            passed &= ~shift<SOUTH>(passed);
        } else {
            passed &= ~(shift<NORTH>(theirPawns) | shift<NORTH_WEST>(theirPawns) | shift<NORTH_EAST>(theirPawns));
            passed &= ~shift<NORTH>(passed);
        }
        threats[c] += popcount(passed) * 30;
    }
    
    Color us = pos.side_to_move();
    int eval = (material[us] - material[~us]) 
             + (mobility[us] - mobility[~us]) / 2
             + (kingSafety[us] - kingSafety[~us])
             + (threats[us] - threats[~us]) / 2;
    
    // Tempo bonus
    eval += 15;
    
    return Value(eval);
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
    state.staticState.allPiecesCount = pos.count<ALL_PIECES>() > 20;
    state.staticState.legalMoveCount = uint8_t(MoveList<LEGAL>(pos).size());
    state.staticState.highMaterial = state.staticState.allPiecesCount;
    updateDynamicState(pos);
    currentStyle = classifyPosition(pos);
}

void ShashinManager::updateDynamicState(const Position& pos) {
    const auto& staticState = state.staticState;
    auto& dynamic = state.dynamicDerived;
    dynamic.isStrategical = !staticState.stmKingExposed && !staticState.opponentKingExposed
                          && !staticState.isSacrificial && !staticState.kingDanger;
    dynamic.isAggressive = staticState.stmKingExposed || staticState.opponentKingExposed
                         || staticState.kingDanger || staticState.isSacrificial;
    dynamic.isTactical = staticState.kingDanger || staticState.isSacrificial
                       || staticState.pawnsNearPromotion;
    dynamic.isTacticalReactive = staticState.opponentKingExposed || 
                                 (staticState.kingDanger && staticState.stmKingExposed);
    dynamic.isHighTal = staticState.stmKingExposed && staticState.opponentKingExposed
                      && staticState.kingDanger;
    dynamic.isComplex = pos.count<ALL_PIECES>() > 16 && 
                       !dynamic.isStrategical && !dynamic.isAggressive;
    dynamic.isMCTSApplicable = dynamic.isHighTal && 
                               staticState.allPiecesCount &&
                               staticState.legalMoveCount > 10;
}

void ShashinManager::updateRootShashinState(Value score, const Position& pos, 
                                           Depth depth, Depth rootDepth) {
    (void)score;
    (void)depth;
    (void)rootDepth;
    updateDynamicState(pos);
    MoveConfig::isStrategical = isStrategical();
    MoveConfig::isAggressive = isAggressive();
    MoveConfig::isFortress = isFortress(pos);
}

ShashinStyle ShashinManager::classifyPosition(const Position& pos) const {
    const auto& dynamic = state.dynamicDerived;
    if (dynamic.isHighTal)
        return HIGH_TAL;
    else if (dynamic.isAggressive && !dynamic.isStrategical)
        return TAL;
    else if (dynamic.isStrategical && dynamic.isAggressive)
        return CAPABLANCA;
    else if (dynamic.isStrategical && !dynamic.isAggressive)
        return PETROSIAN;
    else if (isFortress(pos))
        return HIGH_PETROSIAN;
    else
        return UNKNOWN_STYLE;
}

void ShashinManager::updateCurrentStyle(const Position& pos) {
    currentStyle = classifyPosition(pos);
}

bool ShashinManager::isStrategical() const {
    return state.dynamicDerived.isStrategical;
}

bool ShashinManager::isAggressive() const {
    return state.dynamicDerived.isAggressive;
}

bool ShashinManager::isTal() const {
    return isAggressive() && !isStrategical();
}

bool ShashinManager::isPetrosian() const {
    return isStrategical() && !isAggressive();
}

bool ShashinManager::isCapablanca() const {
    return isStrategical() && isAggressive();
}

bool ShashinManager::isTactical() const {
    return state.dynamicDerived.isTactical;
}

bool ShashinManager::isComplexPosition() const {
    return state.dynamicDerived.isComplex;
}

bool ShashinManager::isFortress(const Position& pos) const {
    if (pos.count<ALL_PIECES>() > 12)
        return false;
    Bitboard pawns = pos.pieces(PAWN);
    Bitboard blockedPawns = (shift<NORTH>(pawns) | shift<SOUTH>(pawns)) & pos.pieces();
    if (popcount(blockedPawns) >= 4)
        return true;
    if (pos.count<BISHOP>() >= 2 && pos.count<PAWN>() <= 4)
        return true;
    return false;
}

bool ShashinManager::isMCTSApplicableByValue() const {
    // Strict criteria: only in HIGH_TAL with many pieces and options
    return state.dynamicDerived.isHighTal && 
           state.staticState.allPiecesCount &&
           state.staticState.legalMoveCount > 10;
}

bool ShashinManager::isMCTSExplorationApplicable() const {
    return isComplexPosition() || isHighPieceDensityCapablancaPosition();
}

bool ShashinManager::isHighPieceDensityCapablancaPosition() const {
    return state.staticState.highMaterial && isCapablanca();
}

bool ShashinManager::isTalTacticalHighMiddle() const {
    return isTal() && state.staticState.highMaterial;
}

bool ShashinManager::isTacticalDefensive() const {
    return state.dynamicDerived.isTacticalReactive && isPetrosian();
}

bool ShashinManager::isLowActivity() const {
    return false;
}

const char* ShashinManager::getStyleName() const {
    switch (currentStyle) {
        case HIGH_TAL:     return "High Tal (Ultra Attacking)";
        case TAL:          return "Tal (Attacking)";
        case CAPABLANCA:   return "Capablanca (Balanced)";
        case PETROSIAN:    return "Petrosian (Strategic)";
        case HIGH_PETROSIAN: return "High Petrosian (Fortress)";
        default:           return "Balanced";
    }
}

const char* ShashinManager::getStyleEmoji() const {
    switch (currentStyle) {
        case HIGH_TAL:     return "[FIRE]";
        case TAL:          return "[SWORD]";
        case CAPABLANCA:   return "[SCALE]";
        case PETROSIAN:    return "[SHIELD]";
        case HIGH_PETROSIAN: return "[CASTLE]";
        default:           return "[SCALE]";
    }
}

bool ShashinManager::avoidStep10() const {
    return (isStrategical() && state.staticState.kingDanger);
}

bool ShashinManager::allowCrystalProbCut() const {
    return isTal() || isComplexPosition();
}

bool ShashinManager::useStep17CrystalLogic() const {
    return isTal() || (isComplexPosition() && state.staticState.kingDanger);
}

Value ShashinManager::static_value(const Position& pos) {
    (void)pos;
    return VALUE_NONE;
}

Move ShashinManager::runMCTSSearch(Position& pos, int iterations) {
    if (!config.useMCTS || !isMCTSApplicableByValue()) {
        return Move::none();
    }
    
    // Use fewer iterations for speed - quality tradeoff
    int actualIterations = std::min(iterations, 150); // Reduced from 500
    
    MCTSTree tree(actualIterations, config.mctsExploration, currentStyle, 
                  nullptr, nullptr); // NNUE not used for now
    Move bestMove = tree.search(pos);
    
    return bestMove;
}

bool ShashinManager::detectKingExposed(const Position& pos, Color side) const {
    Square kingSq = pos.square<KING>(side);
    Bitboard pawnShield = 0;
    if (side == WHITE) {
        if (rank_of(kingSq) >= RANK_2 && rank_of(kingSq) <= RANK_4) {
            pawnShield = pos.pieces(side, PAWN) & (shift<SOUTH>(kingSq) | 
                        shift<SOUTH_WEST>(kingSq) | shift<SOUTH_EAST>(kingSq));
        }
    } else {
        if (rank_of(kingSq) >= RANK_5 && rank_of(kingSq) <= RANK_7) {
            pawnShield = pos.pieces(side, PAWN) & (shift<NORTH>(kingSq) | 
                        shift<NORTH_WEST>(kingSq) | shift<NORTH_EAST>(kingSq));
        }
    }
    if (!pawnShield)
        return true;
    File kingFile = file_of(kingSq);
    Bitboard fileMask = file_bb(kingFile);
    if (kingFile > FILE_A) fileMask |= file_bb(File(kingFile - 1));
    if (kingFile < FILE_H) fileMask |= file_bb(File(kingFile + 1));
    Bitboard pawnsOnFile = pos.pieces(PAWN) & fileMask;
    if (!pawnsOnFile)
        return true;
    return false;
}

bool ShashinManager::detectSacrificial(const Position& pos) const {
    Color us = pos.side_to_move();
    Square enemyKing = pos.square<KING>(~us);
    Bitboard attackZone = attacks_bb<KING>(enemyKing);
    Bitboard ourPieces = pos.pieces(us);
    if ((attackZone & ourPieces) && popcount(attackZone & ourPieces) >= 2)
        return true;
    Bitboard ourQueens = pos.pieces(us, QUEEN);
    if (ourQueens) {
        Square queenSq = lsb(ourQueens);
        if (distance(queenSq, enemyKing) <= 3)
            return true;
    }
    return false;
}

bool ShashinManager::detectKingDanger(const Position& pos) const {
    Color us = pos.side_to_move();
    Square ourKing = pos.square<KING>(us);
    Bitboard kingZone = attacks_bb<KING>(ourKing);
    int attackers = 0;
    for (PieceType pt = KNIGHT; pt <= QUEEN; ++pt) {
        Bitboard pieces = pos.pieces(~us, pt);
        while (pieces) {
            Square sq = pop_lsb(pieces);
            if (attacks_bb(pt, sq, pos.pieces()) & kingZone)
                attackers++;
        }
    }
    return attackers >= 2;
}

bool ShashinManager::detectPawnsNearPromotion(const Position& pos) const {
    Bitboard whitePawns = pos.pieces(WHITE, PAWN);
    Bitboard blackPawns = pos.pieces(BLACK, PAWN);
    if ((whitePawns & (Rank6BB | Rank7BB)) != 0)
        return true;
    if ((blackPawns & (Rank2BB | Rank3BB)) != 0)
        return true;
    return false;
}

int ShashinManager::calculateActivity(const Position& pos) const {
    int activity = 0;
    for (PieceType pt = KNIGHT; pt <= QUEEN; ++pt) {
        Bitboard pieces = pos.pieces(pt);
        activity += popcount(pieces) * 10;
    }
    return activity;
}

} // namespace Stockfish
