/*
  Nextfish - Shashin Theory Integration
  Derived from ShashChess - Shashin position classification system
  Copyright (C) 2004-2025 Andrea Manzo, F. Ferraguti, K.Kiniama and ShashChess developers
  Adapted for Nextfish
*/

#ifndef SHASHIN_H_INCLUDED
#define SHASHIN_H_INCLUDED

#include "types.h"
#include "position.h"
#include "evaluate.h"
#include "nnue/network.h"
#include "nnue/nnue_accumulator.h"

#include <vector>
#include <memory>
#include <atomic>
#include <random>

namespace Stockfish {

// Shashin position styles
enum ShashinStyle {
    HIGH_TAL = 0,
    TAL = 1,
    CAPABLANCA = 2,
    PETROSIAN = 3,
    HIGH_PETROSIAN = 4,
    UNKNOWN_STYLE = 5
};

// Internal MCTS Stack structure
struct MCTSStack {
    StateInfo states[MAX_PLY];
    Move moves[MAX_PLY];
    int depth = 0;

    void push(Position& pos, Move m);
    void pop(Position& pos);
    void clear(Position& pos);
};

// MCTS Node structure
struct MCTSNode {
    Move move;
    MCTSNode* parent;
    std::vector<std::unique_ptr<MCTSNode>> children;
    
    std::atomic<int> visits{0};
    std::atomic<double> totalScore{0.0};
    std::atomic<double> prior{0.0};
    double priorScore = 0.5;
    
    bool isExpanded = false;
    bool isTerminal = false;
    
    MCTSNode(Move m = Move::none(), MCTSNode* par = nullptr, double movePrior = 0.5);
    ~MCTSNode() = default;
    
    double uctScore(double explorationConstant) const;
    MCTSNode* bestChild(double explorationConstant) const;
    bool isFullyExpanded(int legalMoveCount) const;
    MCTSNode* addChild(Move m, double movePrior = 0.5);
};

struct MCTSRootStat {
    Move move = Move::none();
    int visits = 0;
    double winRate = 0.5;
};

// MCTS Tree class
class MCTSTree {
public:
    MCTSTree(int iterations, double exploration, ShashinStyle style);
    ~MCTSTree() = default;
    
    Move search(Position& rootPos, const Eval::NNUE::Networks& networks, double* outWinRate = nullptr, 
                int* outVisits = nullptr, int* outRootVisits = nullptr,
                std::vector<MCTSRootStat>* outRootStats = nullptr);

    MCTSNode* select(MCTSNode* node, Position& rootPos, MCTSStack& stack);
    MCTSNode* expandWithPrior(MCTSNode* node, Position& rootPos, MCTSStack& stack);
    double simulateNNUE(MCTSNode* node, Position& rootPos, const Eval::NNUE::Networks& networks, MCTSStack& stack);
    void backpropagate(MCTSNode* node, double score);

    Move getBestMove(MCTSNode* root, double* outWinRate = nullptr, int* outVisits = nullptr,
                     int* outRootVisits = nullptr) const;

private:
    struct MovePriorCandidate {
        Move move = Move::none();
        double prior = 0.5;
    };

    int maxIterations;
    double explorationConstant;
    ShashinStyle style;
    mutable std::mt19937 rng;
    int maxSimDepth = 4;
    int quiescenceDepth = 4;

    double calculateMovePrior(Position& pos, Move move) const;
    bool isNoisyMove(const Position& pos, Move move) const;
    Move selectDiversifiedMove(const std::vector<MovePriorCandidate>& candidates, int ply);
    double evaluateNNUE(const Position& pos, Color rootSide, const Eval::NNUE::Networks& networks) const;
    std::vector<Move> getPathFromRoot(MCTSNode* node) const;
    void collectRootStats(MCTSNode* root, std::vector<MCTSRootStat>* outRootStats) const;
};

// Shashin Manager and other structs (same as before)
struct DynamicShashinState {
    bool isStrategical = false;
    bool isAggressive = false;
    bool isTactical = false;
    bool isTacticalReactive = false;
    bool isHighTal = false;
    bool isComplex = false;
    bool isMCTSApplicable = false;
};

struct StaticShashinState {
    bool stmKingExposed = false;
    bool opponentKingExposed = false;
    bool isSacrificial = false;
    bool kingDanger = false;
    bool highMaterial = false;
    bool pawnsNearPromotion = false;
    int allPiecesCount = 0;
    uint8_t legalMoveCount = 0;
};

struct RootShashinState {
    StaticShashinState staticState;
    DynamicShashinState dynamicDerived;
};

struct MoveConfig {
    static bool isStrategical;
    static bool isAggressive;
    static bool isFortress;
};

class ShashinManager {
public:
    ShashinManager();
    ~ShashinManager();

    void setStaticState(const Position& pos);
    void updateRootShashinState(Value score, const Position& pos, Depth depth, Depth rootDepth);
    void updateDynamicState(const Position& pos);
    
    const RootShashinState& getState() const { return state; }
    
    ShashinStyle classifyPosition(const Position& pos) const;
    bool isStrategical() const;
    bool isAggressive() const;
    bool isTal() const;
    bool isPetrosian() const;
    bool isCapablanca() const;
    bool isTactical() const;
    bool isComplexPosition() const;
    bool isFortress(const Position& pos) const;
    bool isMCTSApplicableByValue() const;
    bool isMCTSExplorationApplicable() const;
    
    const char* getStyleName() const;
    const char* getStyleEmoji() const;
    void updateCurrentStyle(const Position& pos);

    bool avoidStep10() const;
    bool allowCrystalProbCut() const;
    bool useStep17CrystalLogic() const;
    
    Value static_value(const Position& pos);
    
    Move runMCTSSearch(Position& pos, const Eval::NNUE::Networks& networks, int iterations = 1000, 
                       double* outWinRate = nullptr, int* outVisits = nullptr, int* outRootVisits = nullptr,
                       std::vector<MCTSRootStat>* outRootStats = nullptr);
    
    void syncMCTSOptions(bool enabled, int iterations);

private:
    RootShashinState state;
    ShashinStyle currentStyle = UNKNOWN_STYLE;
    struct {
        bool useMCTS = false;
        int mctsIterations = 1000;
        double mctsExploration = 1.414;
    } config;
    
    bool detectKingExposed(const Position& pos, Color side) const;
    bool detectSacrificial(const Position& pos) const;
    bool detectKingDanger(const Position& pos) const;
    bool detectPawnsNearPromotion(const Position& pos) const;
    int calculateActivity(const Position& pos) const;
};

} // namespace Stockfish

#endif // SHASHIN_H_INCLUDED
