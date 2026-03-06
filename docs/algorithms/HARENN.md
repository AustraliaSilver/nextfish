

# HARENN: HARE-Integrated Neural Network for Horizon-Aware Evaluation and Reduction

## Multi-Output NNUE Training Architecture Fused With HARE

---

## I. Tại Sao NNUE + HARE Là Điểm Tích Hợp Tối Ưu

### 1.1 Điểm Yếu Cấu Trúc Của NNUE Hiện Tại

```
┌──────────────────────────────────────────────────────────────────────────┐
│                CURRENT NNUE: SINGLE-PURPOSE ARCHITECTURE                 │
│                                                                          │
│  Input: Piece placements (HalfKAv2)                                     │
│                    │                                                     │
│                    ▼                                                     │
│  ┌──────────────────────────────────────┐                               │
│  │     Feature Transformer (1024)       │ ← Incrementally updated       │
│  │     (accumulator per side)           │                               │
│  └──────────────────┬───────────────────┘                               │
│                     ▼                                                    │
│  ┌──────────────────────────────────────┐                               │
│  │     Hidden Layer 1 (512→32)          │                               │
│  └──────────────────┬───────────────────┘                               │
│                     ▼                                                    │
│  ┌──────────────────────────────────────┐                               │
│  │     Hidden Layer 2 (32→32)           │                               │
│  └──────────────────┬───────────────────┘                               │
│                     ▼                                                    │
│  ┌──────────────────────────────────────┐                               │
│  │     Output: SINGLE SCALAR (eval)     │ ← Only thing learned         │
│  └──────────────────────────────────────┘                               │
│                                                                          │
│  TRAINING:                                                               │
│    Data: (position, search_score_at_depth_D)                            │
│    Loss: MSE(predicted_eval, search_score)                              │
│    That's it. Nothing else is learned.                                  │
│                                                                          │
│  WHAT'S WASTED:                                                         │
│    The hidden layers IMPLICITLY learn:                                   │
│      - Tactical patterns (but can't extract them)                       │
│      - Position complexity (but can't query it)                         │
│      - King safety specifics (but only as eval component)               │
│      - Piece coordination (but only as eval component)                  │
│                                                                          │
│    This knowledge is LOCKED INSIDE the network                          │
│    and INVISIBLE to the search algorithm.                                │
│                                                                          │
│    Search makes decisions (LMR, pruning, extensions) using              │
│    CRUDE HAND-CRAFTED HEURISTICS while sitting on a neural              │
│    network that already understands the position deeply.                 │
│                                                                          │
│    ╔════════════════════════════════════════════════════════════╗        │
│    ║  NNUE knows the position is tactical.                     ║        │
│    ║  But search uses ln(d)·ln(m) to reduce anyway.            ║        │
│    ║  NNUE knows this move is critical for king safety.        ║        │
│    ║  But LMR reduces it because move_index=15.                ║        │
│    ║  NNUE knows the position needs deep resolution.           ║        │
│    ║  But QSearch uses fixed 6-ply depth.                      ║        │
│    ║                                                           ║        │
│    ║  The neural network and the search are STRANGERS           ║        │
│    ║  sharing the same engine but never communicating.         ║        │
│    ╚════════════════════════════════════════════════════════════╝        │
└──────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Điểm Yếu Cấu Trúc Của HARE Hiện Tại

```
┌──────────────────────────────────────────────────────────────────────────┐
│                HARE WITHOUT NEURAL: EXPENSIVE HEURISTICS                 │
│                                                                          │
│  HARE replaces fixed LMR with intelligent, context-aware reduction.     │
│  But ALL of HARE's intelligence comes from HAND-CRAFTED components:     │
│                                                                          │
│  Component              │ Cost   │ Accuracy │ Could Neural Do Better?   │
│  ───────────────────────┼────────┼──────────┼───────────────────────────│
│  TacticalDensityMap     │ 3-5μs  │ ~70-80%  │ YES — pattern recognition │
│  ThreatLandscapeScanner │ 3-8μs  │ ~60-75%  │ YES — theme detection     │
│  TensionGauge           │ 1-2μs  │ ~65-80%  │ YES — holistic assessment │
│  MoveTacticalRelevance  │ 1-3μs  │ ~55-70%  │ YES — move importance     │
│  HorizonProximityEst.   │ 1-2μs  │ ~50-65%  │ YES — depth prediction    │
│  DefensiveRelevance     │ 1-2μs  │ ~55-70%  │ YES — defensive value     │
│  ThemeIntersection      │ 1-3μs  │ ~50-65%  │ YES — causal reasoning    │
│  ───────────────────────┼────────┼──────────┼───────────────────────────│
│  TOTAL per node         │ 12-25μs│ ~60-72%  │                           │
│                                                                          │
│  Problems with hand-crafted:                                            │
│  1. Hard-coded patterns miss novel positions                            │
│  2. Weights between features are guesses, not learned                   │
│  3. Each component is independent — no shared representation            │
│  4. Updating requires manual engineering, not data-driven               │
│  5. Diminishing returns: improving 70% → 85% accuracy takes 10× effort │
│  6. Computational cost adds up across millions of nodes                 │
│                                                                          │
│  ╔════════════════════════════════════════════════════════════════╗     │
│  ║  Neural network ALREADY has rich position understanding.      ║     │
│  ║  We're building PARALLEL understanding from scratch in HARE.  ║     │
│  ║  This is REDUNDANT and WASTEFUL.                              ║     │
│  ║                                                               ║     │
│  ║  Solution: TEACH the neural network to output what HARE needs.║     │
│  ║  One inference → eval + tactical info + reduction guidance.   ║     │
│  ╚════════════════════════════════════════════════════════════════╝     │
└──────────────────────────────────────────────────────────────────────────┘
```

### 1.3 The Fusion Opportunity

```
╔══════════════════════════════════════════════════════════════════════════╗
║                    THE HARENN INSIGHT                                    ║
║                                                                          ║
║  NNUE's hidden layers ALREADY encode tactical understanding.             ║
║  HARE NEEDS tactical understanding for reduction decisions.              ║
║                                                                          ║
║  Instead of:                                                             ║
║    NNUE(position) → eval                    (wastes knowledge)          ║
║    HARE_heuristics(position) → reduction    (rebuilds knowledge)        ║
║                                                                          ║
║  Do:                                                                     ║
║    HARENN(position) → eval + tactical_complexity + move_criticality     ║
║                       + horizon_risk + resolution_score                  ║
║                                                                          ║
║  ONE forward pass. SHARED features. LEARNED weights.                     ║
║  Neural replaces ~80% of HARE's hand-crafted components.                ║
║  Faster (1 inference vs 7 separate computations).                       ║
║  More accurate (learned from millions of positions vs hand-tuned).      ║
║  Self-improving (retrain with more data → better decisions).            ║
╚══════════════════════════════════════════════════════════════════════════╝
```

---

## II. HARENN Network Architecture

### 2.1 High-Level Design

```
┌──────────────────────────────────────────────────────────────────────────┐
│                      HARENN NETWORK ARCHITECTURE                         │
│          Horizon-Aware Reduction Engine Neural Network                    │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                    INPUT FEATURES                                  │  │
│  │                                                                    │  │
│  │  ┌────────────────────┐  ┌────────────────────────────────────┐  │  │
│  │  │ Standard HalfKA    │  │ HARE Tactical Feature Channels    │  │  │
│  │  │ (piece-square ×    │  │                                    │  │  │
│  │  │  king bucket)      │  │ • Attack map features (32)         │  │  │
│  │  │                    │  │ • Pawn structure features (16)     │  │  │
│  │  │ Dim: 45056         │  │ • King zone features (24)          │  │  │
│  │  │                    │  │ • Piece mobility features (16)     │  │  │
│  │  │                    │  │ • Alignment features (12)          │  │  │
│  │  │                    │  │                                    │  │  │
│  │  │                    │  │ Dim: 100                            │  │  │
│  │  └────────┬───────────┘  └─────────────────┬──────────────────┘  │  │
│  │           │                                 │                     │  │
│  └───────────┼─────────────────────────────────┼─────────────────────┘  │
│              ▼                                 ▼                        │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │              SHARED FEATURE BACKBONE                               │  │
│  │                                                                    │  │
│  │  ┌─────────────────────────────────────────────────┐              │  │
│  │  │  Feature Transformer (45056 → 1024)             │              │  │
│  │  │  [Incrementally updated — same as Stockfish]    │              │  │
│  │  │  Accumulator_white[1024], Accumulator_black[1024]│              │  │
│  │  └────────────────────┬────────────────────────────┘              │  │
│  │                       ▼                                            │  │
│  │  ┌─────────────────────────────────────────────────┐              │  │
│  │  │  Tactical Feature Encoder (100 → 64)            │  NEW        │  │
│  │  │  [Non-incremental, cheap, per-node]             │              │  │
│  │  └────────────────────┬────────────────────────────┘              │  │
│  │                       ▼                                            │  │
│  │  ┌─────────────────────────────────────────────────┐              │  │
│  │  │  Fusion Layer                                    │              │  │
│  │  │  Concat(Acc_w[1024], Acc_b[1024], Tac[64])      │              │  │
│  │  │  → Dense(2112 → 512) → ClippedReLU              │  NEW        │  │
│  │  └────────────────────┬────────────────────────────┘              │  │
│  │                       ▼                                            │  │
│  │  ┌─────────────────────────────────────────────────┐              │  │
│  │  │  Shared Hidden Block                             │              │  │
│  │  │  Dense(512 → 256) → ClippedReLU                 │  WIDENED    │  │
│  │  │  Dense(256 → 128) → ClippedReLU                 │  NEW LAYER  │  │
│  │  └────────────────────┬────────────────────────────┘              │  │
│  │                       │                                            │  │
│  │              ┌────────┴────────┐                                  │  │
│  │              ▼                 ▼                                    │  │
│  │    [Shared_128]          [Shared_128]                             │  │
│  │    for eval heads        for search heads                         │  │
│  └──────┼───────────────────────┼────────────────────────────────────┘  │
│         ▼                       ▼                                        │
│  ┌──────────────┐  ┌─────────────────────────────────────────────────┐  │
│  │              │  │                                                 │  │
│  │   HEAD 1:    │  │   SEARCH DECISION HEADS                        │  │
│  │   EVALUATION │  │                                                 │  │
│  │              │  │  ┌─────────┐ ┌──────────┐ ┌──────────────────┐│  │
│  │  128→32→1    │  │  │ HEAD 2: │ │ HEAD 3:  │ │ HEAD 4:          ││  │
│  │              │  │  │TACTICAL │ │ MOVE     │ │ HORIZON          ││  │
│  │  Output:     │  │  │COMPLEX. │ │ CRITICAL.│ │ RISK             ││  │
│  │  eval (cp)   │  │  │         │ │          │ │                  ││  │
│  │              │  │  │128→32→1 │ │128→64→   │ │ 128→32→1         ││  │
│  │              │  │  │         │ │ 64×64    │ │                  ││  │
│  │              │  │  │Output:  │ │          │ │ Output:          ││  │
│  │              │  │  │τ∈[0,1]  │ │Output:   │ │ ρ∈[0,1]          ││  │
│  │              │  │  │         │ │MCS map   │ │                  ││  │
│  │              │  │  │         │ │(from,to) │ │                  ││  │
│  └──────────────┘  │  └─────────┘ └──────────┘ └──────────────────┘│  │
│                    │                                                 │  │
│                    │  ┌──────────────────────────────────────────┐  │  │
│                    │  │ HEAD 5: RESOLUTION SCORE (for DQRS)      │  │  │
│                    │  │ 128→16→1   Output: rs∈[0,1]              │  │  │
│                    │  └──────────────────────────────────────────┘  │  │
│                    └─────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Detailed Layer Specifications

```python
class HARENNNetwork(nn.Module):
    """HARENN: Multi-head NNUE for HARE integration
    
    Outputs 5 heads from shared backbone:
    1. Evaluation (standard NNUE)
    2. Tactical Complexity (τ)
    3. Move Criticality Scores (MCS map)
    4. Horizon Risk (ρ)
    5. Resolution Score (rs)
    """
    
    # ═══ ARCHITECTURE CONSTANTS ═══
    
    # Feature transformer (incrementally updated)
    FT_INPUT_DIM = 45056      # HalfKAv2: 64 king buckets × 704
    FT_OUTPUT_DIM = 1024      # Accumulator size per side
    
    # Tactical feature encoder (per-node, not incremental)
    TAC_INPUT_DIM = 100       # Hand-crafted tactical features
    TAC_HIDDEN_DIM = 64       # Compact tactical embedding
    
    # Fusion layer
    FUSION_INPUT = 1024 + 1024 + 64  # White_acc + Black_acc + Tactical
    FUSION_OUTPUT = 512
    
    # Shared hidden
    SHARED_H1 = 256
    SHARED_H2 = 128
    
    # Head dimensions
    EVAL_HIDDEN = 32
    TAC_HIDDEN = 32
    MCS_HIDDEN = 64           # Move Criticality needs more capacity
    MCS_OUTPUT = 64 * 64      # From-To scores
    HORIZON_HIDDEN = 32
    RESOLUTION_HIDDEN = 16
    
    def __init__(self):
        super().__init__()
        
        # ═══ FEATURE TRANSFORMER ═══
        # Same as Stockfish: incrementally updated accumulator
        
        self.feature_transformer = FeatureTransformer(
            input_dim=self.FT_INPUT_DIM,
            output_dim=self.FT_OUTPUT_DIM,
            # Uses sparse input, efficient weight lookup
        )
        
        # ═══ TACTICAL FEATURE ENCODER ═══
        # Small dense network for tactical features
        # NOT incrementally updated (changes every node)
        
        self.tactical_encoder = nn.Sequential(
            nn.Linear(self.TAC_INPUT_DIM, self.TAC_HIDDEN_DIM),
            ClippedReLU(),
        )
        
        # ═══ FUSION LAYER ═══
        # Merges piece-placement features with tactical features
        
        self.fusion = nn.Sequential(
            nn.Linear(self.FUSION_INPUT, self.FUSION_OUTPUT),
            ClippedReLU(),
        )
        
        # ═══ SHARED HIDDEN BLOCK ═══
        
        self.shared_h1 = nn.Sequential(
            nn.Linear(self.FUSION_OUTPUT, self.SHARED_H1),
            ClippedReLU(),
        )
        
        self.shared_h2 = nn.Sequential(
            nn.Linear(self.SHARED_H1, self.SHARED_H2),
            ClippedReLU(),
        )
        
        # ═══ HEAD 1: EVALUATION ═══
        
        self.eval_head = nn.Sequential(
            nn.Linear(self.SHARED_H2, self.EVAL_HIDDEN),
            ClippedReLU(),
            nn.Linear(self.EVAL_HIDDEN, 1),
        )
        
        # ═══ HEAD 2: TACTICAL COMPLEXITY ═══
        
        self.tactical_head = nn.Sequential(
            nn.Linear(self.SHARED_H2, self.TAC_HIDDEN),
            ClippedReLU(),
            nn.Linear(self.TAC_HIDDEN, 1),
            nn.Sigmoid(),  # Output ∈ [0, 1]
        )
        
        # ═══ HEAD 3: MOVE CRITICALITY SCORES ═══
        # Outputs a 64×64 map: for each (from_sq, to_sq) pair,
        # how critical is that move? Higher = less safe to reduce.
        #
        # This is the KEY innovation: instead of per-move inference,
        # we compute ALL move criticality scores in ONE forward pass.
        
        self.mcs_head = nn.Sequential(
            nn.Linear(self.SHARED_H2, self.MCS_HIDDEN),
            ClippedReLU(),
            nn.Linear(self.MCS_HIDDEN, self.MCS_OUTPUT),
            nn.Sigmoid(),  # Each score ∈ [0, 1]
        )
        
        # ═══ HEAD 4: HORIZON RISK ═══
        
        self.horizon_head = nn.Sequential(
            nn.Linear(self.SHARED_H2, self.HORIZON_HIDDEN),
            ClippedReLU(),
            nn.Linear(self.HORIZON_HIDDEN, 1),
            nn.Sigmoid(),  # Output ∈ [0, 1]
        )
        
        # ═══ HEAD 5: RESOLUTION SCORE ═══
        
        self.resolution_head = nn.Sequential(
            nn.Linear(self.SHARED_H2, self.RESOLUTION_HIDDEN),
            ClippedReLU(),
            nn.Linear(self.RESOLUTION_HIDDEN, 1),
            nn.Sigmoid(),  # Output ∈ [0, 1]
        )
    
    def forward(self, white_features, black_features, 
                tactical_features, stm):
        """Full forward pass through all heads
        
        Args:
            white_features: Sparse input for white accumulator
            black_features: Sparse input for black accumulator
            tactical_features: Dense tactical feature vector [100]
            stm: Side to move (0=white, 1=black)
        
        Returns:
            HARENNOutput with all 5 head outputs
        """
        
        # ═══ FEATURE TRANSFORMER ═══
        
        acc_white = self.feature_transformer(white_features)  # [1024]
        acc_black = self.feature_transformer(black_features)  # [1024]
        
        # Perspective: arrange accumulators by side-to-move
        if stm == 0:  # White to move
            acc_stm = acc_white
            acc_opp = acc_black
        else:
            acc_stm = acc_black
            acc_opp = acc_white
        
        # ═══ TACTICAL ENCODER ═══
        
        tac_embedding = self.tactical_encoder(tactical_features)  # [64]
        
        # ═══ FUSION ═══
        
        fused = torch.cat([acc_stm, acc_opp, tac_embedding], dim=-1)
        fused = self.fusion(fused)  # [512]
        
        # ═══ SHARED HIDDEN ═══
        
        h1 = self.shared_h1(fused)   # [256]
        shared = self.shared_h2(h1)  # [128]
        
        # ═══ ALL HEADS ═══
        
        eval_score = self.eval_head(shared)          # [1]
        tactical_complexity = self.tactical_head(shared)  # [1]
        mcs_map = self.mcs_head(shared)              # [4096]
        horizon_risk = self.horizon_head(shared)     # [1]
        resolution = self.resolution_head(shared)    # [1]
        
        return HARENNOutput(
            eval_score=eval_score,
            tactical_complexity=tactical_complexity,
            mcs_map=mcs_map.view(64, 64),
            horizon_risk=horizon_risk,
            resolution_score=resolution,
        )
    
    def inference_eval_only(self, white_features, black_features, stm):
        """Fast path: only compute eval (for non-HARE nodes)"""
        
        acc_white = self.feature_transformer(white_features)
        acc_black = self.feature_transformer(black_features)
        
        if stm == 0:
            acc_stm, acc_opp = acc_white, acc_black
        else:
            acc_stm, acc_opp = acc_black, acc_white
        
        # Skip tactical encoder — use zero vector
        zero_tac = torch.zeros(self.TAC_HIDDEN_DIM)
        
        fused = torch.cat([acc_stm, acc_opp, zero_tac], dim=-1)
        fused = self.fusion(fused)
        h1 = self.shared_h1(fused)
        shared = self.shared_h2(h1)
        
        return self.eval_head(shared)
    
    def inference_hare(self, white_features, black_features,
                        tactical_features, stm):
        """HARE-optimized inference: all heads needed"""
        
        # Full forward pass (all heads computed)
        return self.forward(
            white_features, black_features, 
            tactical_features, stm
        )
    
    def get_move_criticality(self, mcs_map, move):
        """Look up criticality score for a specific move"""
        
        from_sq = move.from_square
        to_sq = move.to_square
        
        return mcs_map[from_sq][to_sq].item()


class FeatureTransformer(nn.Module):
    """Incrementally updated feature transformer.
    
    Same as Stockfish NNUE: sparse input → dense accumulator.
    When a piece moves, only affected weights are updated.
    """
    
    def __init__(self, input_dim, output_dim):
        super().__init__()
        self.weight = nn.Parameter(
            torch.zeros(input_dim, output_dim)
        )
        self.bias = nn.Parameter(torch.zeros(output_dim))
    
    def forward(self, sparse_input):
        """Full computation from sparse features"""
        return clipped_relu(
            sparse_matmul(sparse_input, self.weight) + self.bias
        )
    
    def incremental_update(self, accumulator, added_features, 
                            removed_features):
        """Incremental update: O(changed features) instead of O(all)"""
        
        new_acc = accumulator.clone()
        
        for feature_idx in added_features:
            new_acc += self.weight[feature_idx]
        
        for feature_idx in removed_features:
            new_acc -= self.weight[feature_idx]
        
        return clipped_relu(new_acc)


class ClippedReLU(nn.Module):
    """ReLU clamped to [0, 1] — used in Stockfish NNUE for
    quantization-friendly activation."""
    
    def forward(self, x):
        return torch.clamp(x, 0.0, 1.0)
```

### 2.3 Head 3 Deep Dive: Move Criticality Scores (MCS)

```
╔══════════════════════════════════════════════════════════════════════════╗
║               MOVE CRITICALITY SCORES — THE KEY INNOVATION              ║
║                                                                          ║
║  Problem: HARE needs per-move reduction confidence.                     ║
║  Naive approach: run neural network once per move. EXPENSIVE.           ║
║                                                                          ║
║  HARENN approach: compute a 64×64 "criticality map" in ONE pass.        ║
║                                                                          ║
║  MCS_map[from_sq][to_sq] = criticality of moving piece                  ║
║                             from from_sq to to_sq                       ║
║                                                                          ║
║  Interpretation:                                                         ║
║    0.0 = This move is completely irrelevant → reduce maximally          ║
║    0.3 = Low criticality → standard reduction OK                        ║
║    0.6 = Moderate criticality → reduce less                             ║
║    0.8 = High criticality → reduce minimally                            ║
║    1.0 = Critical move → DO NOT reduce                                  ║
║                                                                          ║
║  Usage in HARE:                                                         ║
║    For any legal move M with from_sq=A, to_sq=B:                        ║
║    MRC = 1.0 - MCS_map[A][B]                                           ║
║    reduction = base_R × MRC × global_factor                             ║
║                                                                          ║
║  Cost: ONE matrix multiply (128→4096) computed ONCE per position.       ║
║  Then: simple table lookup per move → O(1)                              ║
║                                                                          ║
║  This replaces ALL of HARE's per-move analysis:                         ║
║    ✓ MoveTacticalRelevanceScorer     (1-3μs per move → 0)              ║
║    ✓ MoveThreatIntersectionDetector  (1-3μs per move → 0)              ║
║    ✓ DefensiveRelevanceAssessment    (1-2μs per move → 0)              ║
║    ✓ HorizonProximityEstimator       (1-2μs per move → 0)              ║
║                                                                          ║
║  Total per-move HARE cost: 4-10μs → replaced by 0μs table lookup       ║
║  MCS computation cost: ~2-3μs ONCE per position                         ║
║  Net savings: ENORMOUS for positions with 30+ legal moves               ║
╚══════════════════════════════════════════════════════════════════════════╝

MCS Map visualization (example position):

       a    b    c    d    e    f    g    h
  ┌────────────────────────────────────────┐
8 │                                        │  Legend:
7 │      ░░░ .95 ░░░░░░░░░░░░ .87 ░░░     │  .90+ = CRITICAL
6 │  ░░░░░░░░░░░░░░░░░░░░░░ .72 ░░░░░░    │  .70+ = HIGH
5 │  ░░░░░░░░ .88 ░░ .45 ░░░░░░░░░░░░░    │  .40+ = MODERATE
4 │  .23 ░░░░░░░░░░░░ .91 ░░░░░░░░░ .34   │  .40- = LOW
3 │  ░░░░░░ .56 ░░░░░░░░░░░░░░ .15 ░░░    │
2 │  .12 .08 .05 ░░░░░░ .67 ░░░ .33 .11   │  (showing to_sq
1 │  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░    │   criticality for
  └────────────────────────────────────────┘   knight on c3)

Interpretation: Knight on c3 moving to d5 = 0.88 (critical!)
                Knight on c3 moving to a4 = 0.23 (can reduce)
                Knight on c3 moving to b1 = 0.08 (safe to reduce heavily)
```

### 2.4 Quantization Strategy

```python
class HARENNQuantized:
    """Quantized version for engine inference.
    
    NNUE engines use int8/int16 quantization for speed.
    HARENN extends this to all heads.
    """
    
    # ═══ QUANTIZATION SCHEME ═══
    #
    # Feature Transformer: int16 weights, int16 accumulator
    #   (same as Stockfish — critical for incremental update)
    #
    # Tactical Encoder: int8 weights, int16 activations
    #   (small layer, int8 sufficient)
    #
    # Fusion + Shared: int8 weights, int8 activations
    #   (standard NNUE quantization)
    #
    # Head 1 (Eval): int8 → int32 output → divide by scale
    # Head 2 (Tactical): int8 → int8 output (0-255 maps to 0.0-1.0)
    # Head 3 (MCS): int8 → int8 output per (from,to) pair
    # Head 4 (Horizon): int8 → int8 output
    # Head 5 (Resolution): int8 → int8 output
    
    EVAL_SCALE = 400        # Same as Stockfish
    TACTIC_SCALE = 255      # Maps int8 to [0, 1]
    MCS_SCALE = 255
    HORIZON_SCALE = 255
    RESOLUTION_SCALE = 255
    
    def quantize_model(self, float_model):
        """Convert float model to quantized int8/int16"""
        
        qmodel = QuantizedHARENNModel()
        
        # Feature transformer: int16 (high precision for incremental)
        qmodel.ft_weights = quantize_to_int16(
            float_model.feature_transformer.weight,
            scale=127  # Stockfish FT scale
        )
        qmodel.ft_bias = quantize_to_int16(
            float_model.feature_transformer.bias,
            scale=127
        )
        
        # Tactical encoder: int8
        qmodel.tac_weights = quantize_to_int8(
            float_model.tactical_encoder[0].weight,
            scale=64
        )
        qmodel.tac_bias = quantize_to_int16(
            float_model.tactical_encoder[0].bias,
            scale=64
        )
        
        # Fusion: int8
        qmodel.fusion_weights = quantize_to_int8(
            float_model.fusion[0].weight,
            scale=64
        )
        
        # Shared hidden: int8
        qmodel.shared_h1_weights = quantize_to_int8(
            float_model.shared_h1[0].weight, scale=64
        )
        qmodel.shared_h2_weights = quantize_to_int8(
            float_model.shared_h2[0].weight, scale=64
        )
        
        # All heads: int8
        for head_name in ['eval', 'tactical', 'mcs', 'horizon', 'resolution']:
            head = getattr(float_model, f'{head_name}_head')
            for i, layer in enumerate(head):
                if isinstance(layer, nn.Linear):
                    setattr(qmodel, f'{head_name}_w{i}',
                           quantize_to_int8(layer.weight, scale=64))
                    setattr(qmodel, f'{head_name}_b{i}',
                           quantize_to_int16(layer.bias, scale=64))
        
        return qmodel
    
    def inference_int8(self, qmodel, acc_white, acc_black, 
                        tac_features, stm):
        """Quantized inference using int8/int16 arithmetic.
        
        This runs on CPU without any floating point!
        Uses SIMD instructions (SSE/AVX2/AVX-512) for speed.
        """
        
        # Step 1: Arrange accumulators by perspective
        if stm == 0:
            acc_stm = acc_white   # int16[1024]
            acc_opp = acc_black   # int16[1024]
        else:
            acc_stm = acc_black
            acc_opp = acc_white
        
        # Step 2: Tactical encoding (int8 matmul)
        tac_emb = int8_matmul(tac_features, qmodel.tac_weights)
        tac_emb = int16_add(tac_emb, qmodel.tac_bias)
        tac_emb = clipped_relu_int16(tac_emb)
        # Result: int16[64]
        
        # Step 3: Fusion (concat + matmul)
        fused_input = concat_int16(acc_stm, acc_opp, tac_emb)
        # int16[2112]
        
        fused = int8_matmul_int16_input(
            fused_input, qmodel.fusion_weights
        )
        fused = clipped_relu_int8(fused)
        # Result: int8[512]
        
        # Step 4: Shared hidden
        h1 = int8_matmul(fused, qmodel.shared_h1_weights)
        h1 = clipped_relu_int8(h1)
        # int8[256]
        
        shared = int8_matmul(h1, qmodel.shared_h2_weights)
        shared = clipped_relu_int8(shared)
        # int8[128]
        
        # Step 5: Heads (all use same shared[128])
        
        # Head 1: Eval
        eval_h = int8_matmul(shared, qmodel.eval_w0)
        eval_h = clipped_relu_int8(eval_h)
        eval_raw = int8_matmul(eval_h, qmodel.eval_w2)
        eval_cp = int32_to_cp(eval_raw, self.EVAL_SCALE)
        
        # Head 2: Tactical complexity
        tac_h = int8_matmul(shared, qmodel.tactical_w0)
        tac_h = clipped_relu_int8(tac_h)
        tac_raw = int8_matmul(tac_h, qmodel.tactical_w2)
        tactical_score = int8_to_float01(tac_raw, self.TACTIC_SCALE)
        
        # Head 3: MCS map
        mcs_h = int8_matmul(shared, qmodel.mcs_w0)
        mcs_h = clipped_relu_int8(mcs_h)
        mcs_raw = int8_matmul(mcs_h, qmodel.mcs_w2)
        # mcs_raw is int8[4096], reshape to [64][64]
        # Keep as int8 — convert to float only when looked up
        mcs_map = reshape_int8(mcs_raw, 64, 64)
        
        # Head 4: Horizon risk
        hor_h = int8_matmul(shared, qmodel.horizon_w0)
        hor_h = clipped_relu_int8(hor_h)
        hor_raw = int8_matmul(hor_h, qmodel.horizon_w2)
        horizon_risk = int8_to_float01(hor_raw, self.HORIZON_SCALE)
        
        # Head 5: Resolution
        res_h = int8_matmul(shared, qmodel.resolution_w0)
        res_h = clipped_relu_int8(res_h)
        res_raw = int8_matmul(res_h, qmodel.resolution_w2)
        resolution = int8_to_float01(res_raw, self.RESOLUTION_SCALE)
        
        return QuantizedHARENNOutput(
            eval_cp=eval_cp,
            tactical_complexity=tactical_score,
            mcs_map_int8=mcs_map,
            horizon_risk=horizon_risk,
            resolution_score=resolution,
        )
```

### 2.5 Parameter Count & Inference Cost

```
┌─────────────────────────────────────┬────────────┬──────────────────────┐
│ Component                           │ Parameters │ Inference (int8 ops) │
├─────────────────────────────────────┼────────────┼──────────────────────┤
│ Feature Transformer (45056→1024)    │ 46,137,344 │ Incremental: ~100    │
│  (same as Stockfish)                │            │ Full: ~45M multiply  │
│                                     │            │                      │
│ Tactical Encoder (100→64)           │ 6,464      │ 6,400 multiply       │
│                                     │            │                      │
│ Fusion Layer (2112→512)             │ 1,081,856  │ 1,081,344 multiply   │
│                                     │            │                      │
│ Shared H1 (512→256)                │ 131,328    │ 131,072 multiply     │
│ Shared H2 (256→128)                │ 32,896     │ 32,768 multiply      │
│                                     │            │                      │
│ Head 1: Eval (128→32→1)            │ 4,129      │ 4,128 multiply       │
│ Head 2: Tactical (128→32→1)        │ 4,129      │ 4,128 multiply       │
│ Head 3: MCS (128→64→4096)          │ 270,592    │ 270,336 multiply     │
│ Head 4: Horizon (128→32→1)         │ 4,129      │ 4,128 multiply       │
│ Head 5: Resolution (128→16→1)      │ 2,065      │ 2,064 multiply      │
├─────────────────────────────────────┼────────────┼──────────────────────┤
│ TOTAL (excl. Feature Transformer)   │ 1,537,588  │ ~1.54M multiply      │
│ Stockfish (excl. FT)                │ ~44,000    │ ~44K multiply        │
│ HARENN overhead vs Stockfish        │ +35×       │ +35× (heads 2-5)    │
├─────────────────────────────────────┼────────────┼──────────────────────┤
│ But with SIMD (AVX-512):           │            │                      │
│ Stockfish eval inference            │            │ ~0.3-0.5 μs          │
│ HARENN eval-only inference          │            │ ~0.5-0.8 μs          │
│ HARENN full inference (all heads)   │            │ ~1.5-3.0 μs          │
│ HARENN MCS lookup per move          │            │ ~0.01 μs             │
│                                     │            │                      │
│ Hand-crafted HARE per node          │            │ ~12-25 μs            │
│ HARENN replaces HARE                │            │ ~1.5-3.0 μs          │
│ SAVINGS per node                    │            │ ~10-22 μs (80-87%)   │
├─────────────────────────────────────┼────────────┼──────────────────────┤
│ Net file size (quantized)           │ ~48 MB     │ (vs ~40MB Stockfish) │
│ RAM usage increase                  │ ~10 MB     │                      │
└─────────────────────────────────────┴────────────┴──────────────────────┘
```

---

## III. Training Data Generation Pipeline

### 3.1 Overview: What Makes HARENN Training Data Special

```
╔══════════════════════════════════════════════════════════════════════════╗
║                HARENN TRAINING DATA: SEARCH-AWARE LABELS                ║
║                                                                          ║
║  Standard NNUE: (position, eval_at_depth_D)                             ║
║  One label, easy to generate.                                            ║
║                                                                          ║
║  HARENN: (position, eval, τ, MCS_map, ρ, rs)                           ║
║  FIVE labels, each requires DIFFERENT generation method.                 ║
║                                                                          ║
║  Label        │ Generation Method                │ Cost per pos.        ║
║  ─────────────┼──────────────────────────────────┼──────────────────────║
║  eval         │ Search at depth D (standard)      │ ~10M nodes          ║
║  τ (tactical) │ Multi-depth search analysis       │ +5M nodes           ║
║  MCS_map      │ Per-move reduction experiment     │ +50M nodes          ║
║  ρ (horizon)  │ Depth-D vs depth-D+4 comparison  │ +30M nodes          ║
║  rs (resolut.)│ QSearch analysis                  │ +2M nodes           ║
║  ─────────────┼──────────────────────────────────┼──────────────────────║
║  TOTAL        │                                  │ ~97M nodes/position ║
║                                                                          ║
║  This is ~10× more expensive than standard NNUE training data.          ║
║  But labels are MUCH richer → trains MUCH better model.                 ║
║  And we only need 1/3 as many positions (more info per position).       ║
║  Net cost: ~3× standard NNUE training cost.                            ║
╚══════════════════════════════════════════════════════════════════════════╝
```

### 3.2 Label Generation: Eval (Head 1)

```python
class EvalLabelGenerator:
    """Standard NNUE eval label generation.
    
    Same as Stockfish gensfen/nodchip approach:
    - Play self-play games
    - At each position, search to depth D
    - Use search score as label
    
    Enhancement for HARENN: use game result blending
    eval_label = λ · search_score + (1-λ) · game_result_value
    where λ = 0.75 (standard)
    """
    
    def generate(self, position, search_depth=8):
        """Generate eval label for a position"""
        
        # Search at standard depth
        result = search(position, depth=search_depth)
        search_score = result.score
        
        # Will be blended with game result during training
        return EvalLabel(
            score=search_score,
            depth=search_depth,
            best_move=result.best_move,
            pv=result.pv,
        )
```

### 3.3 Label Generation: Tactical Complexity (Head 2)

```python
class TacticalComplexityLabelGenerator:
    """Generate tactical complexity labels (τ).
    
    τ ∈ [0, 1]: how tactically complex is this position?
    
    Ground truth definition:
    τ = f(score_volatility, re_search_rate, tactical_pv_depth,
          eval_convergence_rate)
    
    Intuition: a position is tactically complex if:
    - Score changes a lot between depths
    - Many moves need re-searching (LMR failures)
    - PV contains many captures/checks
    - Eval takes many plies to converge
    """
    
    def generate(self, position, max_depth=12):
        """Generate tactical complexity label"""
        
        scores_by_depth = []
        re_search_counts = []
        pv_tactical_density = []
        
        # Search at multiple depths
        for depth in range(4, max_depth + 1):
            result = search_with_stats(position, depth=depth)
            
            scores_by_depth.append(result.score)
            re_search_counts.append(result.re_search_count)
            
            # Count tactical moves in PV
            tactical_in_pv = sum(
                1 for move in result.pv
                if move.is_capture or move.gives_check
            )
            pv_tactical_density.append(
                tactical_in_pv / max(len(result.pv), 1)
            )
        
        # ═══ COMPONENT 1: Score Volatility ═══
        # How much does score change between consecutive depths?
        
        deltas = [abs(scores_by_depth[i] - scores_by_depth[i-1])
                  for i in range(1, len(scores_by_depth))]
        
        avg_delta = np.mean(deltas)
        max_delta = max(deltas)
        
        # Normalize: 0cp change → 0, 100cp+ change → 1
        score_volatility = sigmoid((avg_delta - 15) / 20)
        
        # ═══ COMPONENT 2: Re-search Rate ═══
        # Higher re-search rate → more tactical uncertainty
        
        if re_search_counts:
            avg_re_search_rate = np.mean(re_search_counts) / 30.0
            re_search_factor = min(avg_re_search_rate, 1.0)
        else:
            re_search_factor = 0.0
        
        # ═══ COMPONENT 3: PV Tactical Density ═══
        
        avg_pv_tactical = np.mean(pv_tactical_density)
        
        # ═══ COMPONENT 4: Eval Convergence ═══
        # How quickly does eval settle?
        
        if len(scores_by_depth) >= 4:
            early_range = max(scores_by_depth[:3]) - min(scores_by_depth[:3])
            late_range = max(scores_by_depth[-3:]) - min(scores_by_depth[-3:])
            
            if early_range > 1:
                convergence_rate = 1.0 - min(late_range / early_range, 1.0)
            else:
                convergence_rate = 1.0  # Already converged
        else:
            convergence_rate = 0.5
        
        # Slow convergence → high tactical complexity
        convergence_factor = 1.0 - convergence_rate
        
        # ═══ COMPONENT 5: Best Move Stability ═══
        
        best_moves = []
        for depth in range(4, max_depth + 1):
            result = search(position, depth=depth)
            best_moves.append(result.best_move)
        
        unique_best = len(set(best_moves[-4:]))  # Last 4 depths
        instability = (unique_best - 1) / 3.0  # 0 if stable, 1 if all different
        
        # ═══ COMBINE ═══
        
        tau = (
            score_volatility * 0.25 +
            re_search_factor * 0.20 +
            avg_pv_tactical * 0.20 +
            convergence_factor * 0.15 +
            instability * 0.20
        )
        
        return TacticalComplexityLabel(
            tau=clamp(tau, 0.0, 1.0),
            components={
                'score_volatility': score_volatility,
                're_search_factor': re_search_factor,
                'pv_tactical_density': avg_pv_tactical,
                'convergence_factor': convergence_factor,
                'instability': instability,
            }
        )
```

### 3.4 Label Generation: Move Criticality Scores (Head 3)

```python
class MCSLabelGenerator:
    """Generate Move Criticality Score labels.
    
    THIS IS THE MOST EXPENSIVE AND MOST IMPORTANT LABEL.
    
    For each legal move in a position, determine:
    "Would reducing this move cause a significant error?"
    
    Method: Compare full-depth score with reduced-depth score.
    If they differ significantly → move is CRITICAL (high MCS).
    If they agree → move is safe to reduce (low MCS).
    """
    
    def generate(self, position, full_depth=12, reduction_amount=3):
        """Generate MCS labels for ALL legal moves"""
        
        # Step 1: Search at full depth to get baseline
        full_result = search(position, depth=full_depth)
        baseline_alpha = full_result.score - 50  # Approximate alpha
        
        # Step 2: For each legal move, compare full vs reduced
        mcs_labels = {}  # (from_sq, to_sq) → criticality ∈ [0, 1]
        
        legal_moves = generate_legal_moves(position)
        
        for move in legal_moves:
            position.make_move(move)
            
            # Full depth search of this move
            full_score = -search(
                position, depth=full_depth - 1
            ).score
            
            # Reduced depth search of this move
            reduced_depth = max(1, full_depth - 1 - reduction_amount)
            reduced_score = -search(
                position, depth=reduced_depth
            ).score
            
            position.unmake_move(move)
            
            # ═══ COMPUTE CRITICALITY ═══
            
            score_diff = abs(full_score - reduced_score)
            
            # Also check: did this move turn out to be best or near-best?
            is_near_best = (full_score >= full_result.score - 30)
            
            # Did reduction miss that this move is important?
            reduction_missed = (
                reduced_score < baseline_alpha and
                full_score > baseline_alpha
            )
            
            # Criticality scoring
            if reduction_missed:
                # Reduction would have MISSED this good move!
                # Highest criticality
                criticality = 0.9 + min(score_diff / 500.0, 0.1)
            elif is_near_best and score_diff > 50:
                # Near-best move with significant reduction error
                criticality = 0.7 + min(score_diff / 300.0, 0.2)
            elif score_diff > 100:
                # Large difference → moderately critical
                criticality = 0.5 + min(score_diff / 500.0, 0.3)
            elif score_diff > 30:
                # Noticeable difference → slightly critical
                criticality = 0.2 + score_diff / 200.0
            else:
                # Small difference → safe to reduce
                criticality = score_diff / 150.0
            
            criticality = clamp(criticality, 0.0, 1.0)
            
            mcs_labels[(move.from_square, move.to_square)] = criticality
        
        # Step 3: Build 64×64 map
        mcs_map = np.zeros((64, 64), dtype=np.float32)
        
        for (from_sq, to_sq), crit in mcs_labels.items():
            mcs_map[from_sq][to_sq] = crit
        
        # Non-legal moves stay at 0 (irrelevant)
        
        return MCSLabel(
            mcs_map=mcs_map,
            num_legal_moves=len(legal_moves),
            num_critical_moves=sum(
                1 for c in mcs_labels.values() if c > 0.7
            ),
        )
    
    def generate_efficient(self, position, full_depth=10, 
                            sample_rate=0.5):
        """More efficient MCS generation: sample moves instead of all.
        
        For training, we don't need EVERY move labeled perfectly.
        Sample 50% of moves, interpolate the rest.
        
        This cuts cost by ~50%.
        """
        
        legal_moves = generate_legal_moves(position)
        
        # Always include first 5 moves (well-ordered, important baseline)
        sampled_moves = list(legal_moves[:5])
        
        # Sample from remaining
        remaining = legal_moves[5:]
        sample_count = max(1, int(len(remaining) * sample_rate))
        sampled_moves.extend(
            random.sample(remaining, min(sample_count, len(remaining)))
        )
        
        mcs_labels = {}
        full_result = search(position, depth=full_depth)
        
        for move in sampled_moves:
            position.make_move(move)
            
            full_score = -search(position, depth=full_depth - 1).score
            reduced_score = -search(position, depth=full_depth - 4).score
            
            position.unmake_move(move)
            
            score_diff = abs(full_score - reduced_score)
            criticality = self.score_diff_to_criticality(
                score_diff, full_score, full_result.score
            )
            
            mcs_labels[(move.from_square, move.to_square)] = criticality
        
        # Unsampled moves: assign low criticality (safe assumption)
        for move in legal_moves:
            key = (move.from_square, move.to_square)
            if key not in mcs_labels:
                mcs_labels[key] = 0.15  # Default: probably safe to reduce
        
        # Build map
        mcs_map = np.zeros((64, 64), dtype=np.float32)
        for (from_sq, to_sq), crit in mcs_labels.items():
            mcs_map[from_sq][to_sq] = crit
        
        return MCSLabel(mcs_map=mcs_map)
    
    def score_diff_to_criticality(self, diff, move_score, best_score):
        """Convert score difference to criticality"""
        
        is_near_best = (move_score >= best_score - 30)
        
        if is_near_best and diff > 50:
            return clamp(0.7 + diff / 300.0, 0.7, 1.0)
        elif diff > 100:
            return clamp(0.4 + diff / 500.0, 0.4, 0.8)
        elif diff > 30:
            return clamp(0.15 + diff / 200.0, 0.15, 0.5)
        else:
            return clamp(diff / 150.0, 0.0, 0.2)
```

### 3.5 Label Generation: Horizon Risk (Head 4)

```python
class HorizonRiskLabelGenerator:
    """Generate horizon risk labels (ρ).
    
    ρ ∈ [0, 1]: how likely is it that the eval at current depth
    is WRONG due to horizon effect?
    
    Ground truth: compare eval at depth D with eval at depth D+4.
    Large difference → high horizon risk.
    
    Also detect SPECIFIC horizon effect patterns:
    - Score suddenly jumps at deeper search (hidden tactic found)
    - Best move changes at deeper search (horizon was hiding truth)
    - PV at deeper depth has important quiet moves (missed by QSearch)
    """
    
    def generate(self, position, base_depth=8, verify_depth=12):
        """Generate horizon risk label"""
        
        # Search at base depth
        base_result = search(position, depth=base_depth)
        base_score = base_result.score
        
        # Search at deeper depth (verification)
        deep_result = search(position, depth=verify_depth)
        deep_score = deep_result.score
        
        # ═══ COMPONENT 1: SCORE DISCREPANCY ═══
        
        score_diff = abs(deep_score - base_score)
        
        # Direction matters: over-optimistic at shallow depth is worse
        # (engine makes moves based on inflated eval → blunder)
        sign_diff = deep_score - base_score
        stm_multiplier = 1 if position.side_to_move == WHITE else -1
        
        # Positive sign_diff means deeper search found BETTER score
        # Negative means shallow was OVER-OPTIMISTIC → horizon effect!
        if sign_diff * stm_multiplier < -30:
            # Shallow search was over-optimistic
            overoptimism_penalty = min(
                abs(sign_diff * stm_multiplier) / 200.0, 0.5
            )
        else:
            overoptimism_penalty = 0.0
        
        score_risk = sigmoid((score_diff - 20) / 40)
        
        # ═══ COMPONENT 2: BEST MOVE CHANGED ═══
        
        move_changed = (base_result.best_move != deep_result.best_move)
        move_risk = 0.3 if move_changed else 0.0
        
        # Even worse if the deep best move was LATE in shallow ordering
        if move_changed:
            shallow_moves = generate_and_order_moves(position)
            deep_best_index = find_move_index(
                shallow_moves, deep_result.best_move
            )
            
            if deep_best_index is not None and deep_best_index > 10:
                move_risk += 0.2  # Deep best was poorly ordered at shallow
        
        # ═══ COMPONENT 3: PV STRUCTURE DIFFERENCE ═══
        
        base_pv = base_result.pv
        deep_pv = deep_result.pv
        
        # Count quiet moves in deep PV that aren't in shallow PV
        deep_quiet_moves = [
            m for m in deep_pv 
            if not m.is_capture and not m.gives_check
        ]
        
        critical_quiets = 0
        for dq in deep_quiet_moves:
            if dq not in base_pv:
                critical_quiets += 1
        
        pv_risk = min(critical_quiets / 4.0, 0.3)
        
        # ═══ COMPONENT 4: MULTI-DEPTH TREND ═══
        
        intermediate_scores = []
        for d in range(base_depth, verify_depth + 1, 2):
            result = search(position, depth=d)
            intermediate_scores.append(result.score)
        
        # Monotone trend → likely genuine improvement, not horizon effect
        # Sudden jump → likely horizon effect resolved
        
        deltas = [abs(intermediate_scores[i] - intermediate_scores[i-1])
                  for i in range(1, len(intermediate_scores))]
        
        if deltas:
            max_delta = max(deltas)
            avg_delta = np.mean(deltas)
            
            # Sudden spike suggests horizon effect
            if max_delta > avg_delta * 3 and max_delta > 30:
                trend_risk = 0.3
            else:
                trend_risk = 0.0
        else:
            trend_risk = 0.0
        
        # ═══ COMBINE ═══
        
        rho = (
            score_risk * 0.30 +
            overoptimism_penalty * 0.20 +
            move_risk * 0.20 +
            pv_risk * 0.15 +
            trend_risk * 0.15
        )
        
        return HorizonRiskLabel(
            rho=clamp(rho, 0.0, 1.0),
            score_at_base=base_score,
            score_at_deep=deep_score,
            move_changed=move_changed,
            overoptimistic=overoptimism_penalty > 0.1,
        )
```

### 3.6 Label Generation: Resolution Score (Head 5)

```python
class ResolutionLabelGenerator:
    """Generate resolution score labels (rs).
    
    rs ∈ [0, 1]: how "resolved" is this position?
    Measures whether QSearch would need significant work.
    
    Ground truth: measured from actual QSearch behavior.
    """
    
    def generate(self, position):
        """Generate resolution score label"""
        
        # Run QSearch and measure characteristics
        qs_result = qsearch_with_stats(position, alpha=-INFINITY, 
                                        beta=INFINITY, max_depth=20)
        
        # ═══ COMPONENT 1: QSEARCH DEPTH NEEDED ═══
        
        depth_needed = qs_result.max_depth_reached
        
        # 0-2 plies → very resolved (0.9-1.0)
        # 3-5 plies → mostly resolved (0.5-0.9)
        # 6+ plies → unresolved (0.0-0.5)
        depth_resolution = 1.0 - min(depth_needed / 10.0, 1.0)
        
        # ═══ COMPONENT 2: QSEARCH NODE COUNT ═══
        
        qs_nodes = qs_result.nodes_searched
        
        # Few nodes → resolved
        # Many nodes → unresolved (complex exchanges, etc.)
        node_resolution = 1.0 - sigmoid(
            (math.log(max(qs_nodes, 1)) - 5) / 2
        )
        
        # ═══ COMPONENT 3: EVAL CHANGE IN QSEARCH ═══
        
        static_eval = nnue_eval(position)
        qs_eval = qs_result.score
        eval_change = abs(qs_eval - static_eval)
        
        # Small change → position was already resolved
        # Large change → QSearch found important tactical info
        eval_resolution = 1.0 - sigmoid((eval_change - 20) / 40)
        
        # ═══ COMPONENT 4: STAND-PAT CUTOFF ═══
        
        if qs_result.stand_pat_cutoff:
            # Immediate cutoff → very resolved
            cutoff_bonus = 0.2
        else:
            cutoff_bonus = 0.0
        
        # ═══ COMBINE ═══
        
        rs = (
            depth_resolution * 0.30 +
            node_resolution * 0.25 +
            eval_resolution * 0.30 +
            cutoff_bonus * 0.15
        )
        
        return ResolutionLabel(
            rs=clamp(rs, 0.0, 1.0),
            qs_depth=depth_needed,
            qs_nodes=qs_nodes,
            eval_change=eval_change,
        )
```

### 3.7 Complete Training Data Pipeline

```python
class HARENNDataPipeline:
    """End-to-end pipeline for generating HARENN training data.
    
    Process:
    1. Play self-play games (standard)
    2. Extract positions from games
    3. For each position, generate all 5 labels
    4. Extract features (HalfKA + tactical)
    5. Write to training file
    """
    
    def __init__(self, engine_path, num_threads=64):
        self.engine = load_engine(engine_path)
        self.num_threads = num_threads
        
        self.eval_gen = EvalLabelGenerator()
        self.tac_gen = TacticalComplexityLabelGenerator()
        self.mcs_gen = MCSLabelGenerator()
        self.horizon_gen = HorizonRiskLabelGenerator()
        self.resolution_gen = ResolutionLabelGenerator()
        self.feature_extractor = HARENNFeatureExtractor()
    
    def generate_game_data(self, num_games=1000):
        """Generate training data from self-play games"""
        
        all_samples = []
        
        for game_id in range(num_games):
            if game_id % 100 == 0:
                print(f"Game {game_id}/{num_games}")
            
            # Play a self-play game
            game = self.play_self_play_game()
            
            # Extract positions (every 4th move to reduce correlation)
            positions = extract_positions(game, skip=4)
            
            for pos, game_result in positions:
                # Skip terminal positions
                if pos.is_checkmate() or pos.is_stalemate():
                    continue
                
                # Skip positions with extreme eval (mates, etc.)
                quick_eval = self.engine.quick_eval(pos)
                if abs(quick_eval) > 3000:
                    continue
                
                # Generate all labels
                sample = self.generate_sample(pos, game_result)
                
                if sample is not None:
                    all_samples.append(sample)
        
        return all_samples
    
    def generate_sample(self, position, game_result):
        """Generate one complete training sample"""
        
        try:
            # Labels (expensive — this is the bottleneck)
            eval_label = self.eval_gen.generate(position, search_depth=9)
            tac_label = self.tac_gen.generate(position, max_depth=11)
            mcs_label = self.mcs_gen.generate_efficient(
                position, full_depth=9, sample_rate=0.4
            )
            horizon_label = self.horizon_gen.generate(
                position, base_depth=8, verify_depth=12
            )
            resolution_label = self.resolution_gen.generate(position)
            
            # Features
            halfka_features = self.feature_extractor.extract_halfka(
                position
            )
            tactical_features = self.feature_extractor.extract_tactical(
                position
            )
            
            return HARENNSample(
                # Features
                white_halfka=halfka_features.white,
                black_halfka=halfka_features.black,
                tactical_features=tactical_features,
                stm=position.side_to_move,
                
                # Labels
                eval_score=eval_label.score,
                game_result=game_result,
                tactical_complexity=tac_label.tau,
                mcs_map=mcs_label.mcs_map,
                horizon_risk=horizon_label.rho,
                resolution_score=resolution_label.rs,
                
                # Metadata
                fen=position.fen(),
                ply=position.ply,
            )
        
        except Exception as e:
            logging.warning(f"Failed to generate sample: {e}")
            return None
    
    def play_self_play_game(self):
        """Play one self-play game with some randomization"""
        
        position = Position()  # Starting position
        
        # Random opening (first 8 plies with some randomization)
        for _ in range(random.randint(2, 8)):
            moves = generate_legal_moves(position)
            if not moves:
                break
            
            # Choose from top 3 moves randomly
            results = []
            for move in moves[:10]:
                position.make_move(move)
                score = -self.engine.quick_eval(position)
                position.unmake_move(move)
                results.append((move, score))
            
            results.sort(key=lambda x: -x[1])
            top_moves = results[:3]
            
            chosen = random.choice(top_moves)
            position.make_move(chosen[0])
        
        # Continue with engine play
        game_moves = []
        
        while not position.is_game_over() and position.ply < 300:
            result = self.engine.search(position, depth=8)
            
            if abs(result.score) > 5000:
                break  # Decisive position
            
            position.make_move(result.best_move)
            game_moves.append(result.best_move)
        
        # Determine game result
        if position.is_checkmate():
            game_result = -1 if position.side_to_move == WHITE else 1
        else:
            game_result = 0  # Draw
        
        return Game(moves=game_moves, result=game_result)
```

---

## IV. Tactical Feature Extraction

### 4.1 The 100-Dimensional Tactical Feature Vector

```python
class HARENNFeatureExtractor:
    """Extract tactical features for HARENN input.
    
    These features supplement the standard HalfKA features
    with EXPLICIT tactical information that helps all 5 heads.
    
    100 features organized in 6 groups:
    - Attack features (32)
    - Pawn structure features (16)
    - King zone features (24)
    - Piece mobility features (16)
    - Alignment/coordination features (12)
    """
    
    def extract_tactical(self, position):
        """Extract 100-dim tactical feature vector"""
        
        features = np.zeros(100, dtype=np.float32)
        idx = 0
        
        # ═══ GROUP 1: ATTACK FEATURES (32) ═══
        
        for side in [WHITE, BLACK]:
            opp = side ^ 1
            
            # f[0-1]: Total attacks on opponent pieces
            attacks_on_pieces = 0
            for sq in position.piece_squares(opp):
                piece = position.piece_at(sq)
                attackers = position.attackers_of(sq, side)
                attacks_on_pieces += popcount(attackers)
            features[idx] = min(attacks_on_pieces / 20.0, 1.0)
            idx += 1
            
            # f[2-3]: Hanging pieces (attacked but not defended)
            hanging = 0
            for sq in position.piece_squares(opp):
                if (position.is_attacked_by(sq, side) and
                    not position.is_defended_by(sq, opp)):
                    hanging += 1
            features[idx] = min(hanging / 4.0, 1.0)
            idx += 1
            
            # f[4-5]: Favorable exchanges available
            fav_exchanges = 0
            for capture in generate_captures_for_side(position, side):
                see_val = see(position, capture)
                if see_val > 50:
                    fav_exchanges += 1
            features[idx] = min(fav_exchanges / 5.0, 1.0)
            idx += 1
            
            # f[6-7]: Checks available
            checks = count_legal_checks(position, side)
            features[idx] = min(checks / 5.0, 1.0)
            idx += 1
            
            # f[8-9]: Fork opportunities
            forks = count_fork_opportunities(position, side)
            features[idx] = min(forks / 3.0, 1.0)
            idx += 1
            
            # f[10-11]: Pin count
            pins = count_pins(position, side)
            features[idx] = min(pins / 4.0, 1.0)
            idx += 1
            
            # f[12-13]: Discovered attack potential
            discoveries = count_discovered_setups(position, side)
            features[idx] = min(discoveries / 3.0, 1.0)
            idx += 1
            
            # f[14-15]: X-ray attacks through pieces
            xrays = count_xray_attacks(position, side)
            features[idx] = min(xrays / 4.0, 1.0)
            idx += 1
        
        # idx = 32
        
        # ═══ GROUP 2: PAWN STRUCTURE FEATURES (16) ═══
        
        for side in [WHITE, BLACK]:
            # f[32-33]: Passed pawns count
            passed = count_passed_pawns(position, side)
            features[idx] = min(passed / 3.0, 1.0)
            idx += 1
            
            # f[34-35]: Most advanced passed pawn rank
            max_rank = most_advanced_passed_pawn_rank(position, side)
            features[idx] = max_rank / 7.0  # 0-7 normalized
            idx += 1
            
            # f[36-37]: Isolated pawns
            isolated = count_isolated_pawns(position, side)
            features[idx] = min(isolated / 4.0, 1.0)
            idx += 1
            
            # f[38-39]: Doubled pawns
            doubled = count_doubled_pawns(position, side)
            features[idx] = min(doubled / 3.0, 1.0)
            idx += 1
            
            # f[40-41]: Pawn tension (capturable pairs)
            tension = count_pawn_tension(position, side)
            features[idx] = min(tension / 6.0, 1.0)
            idx += 1
            
            # f[42-43]: Pawn breaks available
            breaks = count_pawn_breaks(position, side)
            features[idx] = min(breaks / 4.0, 1.0)
            idx += 1
            
            # f[44-45]: Connected passed pawns
            connected = count_connected_passers(position, side)
            features[idx] = min(connected / 2.0, 1.0)
            idx += 1
            
            # f[46-47]: Pawn chain strength
            chain = pawn_chain_score(position, side)
            features[idx] = min(chain / 200.0, 1.0)
            idx += 1
        
        # idx = 48
        
        # ═══ GROUP 3: KING ZONE FEATURES (24) ═══
        
        for side in [WHITE, BLACK]:
            opp = side ^ 1
            king_sq = position.king_square(side)
            
            # f[48-49]: Pawn shield strength
            shield = pawn_shield_score(position, side)
            features[idx] = min(shield / 300.0, 1.0)
            idx += 1
            
            # f[50-51]: Attackers near king
            king_zone = get_king_zone(king_sq)
            attacker_count = 0
            attacker_value = 0
            for sq in king_zone:
                attackers = position.attackers_of(sq, opp)
                attacker_count += popcount(attackers)
                for att_sq in iterate_bits(attackers):
                    attacker_value += piece_value(
                        position.piece_at(att_sq).type
                    )
            features[idx] = min(attacker_count / 15.0, 1.0)
            idx += 1
            features[idx] = min(attacker_value / 3000.0, 1.0)
            idx += 1
            
            # f[54-55]: Defenders near king
            defender_count = 0
            for sq in king_zone:
                defenders = position.defenders_of(sq, side)
                defender_count += popcount(defenders)
            features[idx] = min(defender_count / 15.0, 1.0)
            idx += 1
            
            # f[56-57]: Open files near king
            open_near = count_open_files_near_king(position, king_sq)
            features[idx] = min(open_near / 3.0, 1.0)
            idx += 1
            
            # f[58-59]: King mobility (escape squares)
            king_mobility = count_king_legal_moves(position, side)
            features[idx] = min(king_mobility / 8.0, 1.0)
            idx += 1
        
        # idx = 72
        
        # ═══ GROUP 4: PIECE MOBILITY FEATURES (16) ═══
        
        for side in [WHITE, BLACK]:
            # f[72-73]: Total piece mobility
            total_mobility = count_total_mobility(position, side)
            features[idx] = min(total_mobility / 60.0, 1.0)
            idx += 1
            
            # f[74-75]: Trapped pieces count
            trapped = count_trapped_pieces(position, side)
            features[idx] = min(trapped / 3.0, 1.0)
            idx += 1
            
            # f[76-77]: Pieces on strong squares
            strong = count_pieces_on_outposts(position, side)
            features[idx] = min(strong / 3.0, 1.0)
            idx += 1
            
            # f[78-79]: Piece coordination (pieces defending each other)
            coordination = count_piece_coordination(position, side)
            features[idx] = min(coordination / 10.0, 1.0)
            idx += 1
            
            # f[80-81]: Centralization score
            central = centralization_score(position, side)
            features[idx] = min(central / 300.0, 1.0)
            idx += 1
            
            # f[82-83]: Piece activity differential
            activity = piece_activity_score(position, side)
            features[idx] = min(activity / 400.0, 1.0)
            idx += 1
            
            # f[84-85]: Rook on open/semi-open file
            rook_files = rook_file_score(position, side)
            features[idx] = min(rook_files / 200.0, 1.0)
            idx += 1
            
            # f[86-87]: Bishop pair
            has_pair = has_bishop_pair(position, side)
            features[idx] = 1.0 if has_pair else 0.0
            idx += 1
        
        # idx = 88
        
        # ═══ GROUP 5: ALIGNMENT/COORDINATION (12) ═══
        
        # f[88]: Material imbalance magnitude
        imbalance = abs(material_balance(position))
        features[idx] = min(imbalance / 1000.0, 1.0)
        idx += 1
        
        # f[89]: Game phase (0=opening, 1=endgame)
        features[idx] = game_phase_float(position)
        idx += 1
        
        # f[90]: Total piece count / 32
        features[idx] = popcount(position.occupied) / 32.0
        idx += 1
        
        # f[91]: Queens on board (0, 1, or 2)
        features[idx] = count_queens(position) / 2.0
        idx += 1
        
        # f[92]: Opposite-color bishops
        features[idx] = 1.0 if has_opposite_color_bishops(position) else 0.0
        idx += 1
        
        # f[93]: Total pawn count / 16
        features[idx] = count_all_pawns(position) / 16.0
        idx += 1
        
        # f[94]: Material difference (signed, stm perspective)
        mat_diff = material_balance_stm(position) / 1500.0
        features[idx] = clamp(mat_diff, -1.0, 1.0)
        idx += 1
        
        # f[95]: Castling rights remaining (0-4)
        features[idx] = position.castling_rights_count() / 4.0
        idx += 1
        
        # f[96]: Half-move clock / 100
        features[idx] = min(position.halfmove_clock / 100.0, 1.0)
        idx += 1
        
        # f[97]: Tempo (side to move has +0.5 usually)
        features[idx] = 0.5  # Placeholder, refined by network
        idx += 1
        
        # f[98]: Piece alignment score (batteries, etc.)
        features[idx] = min(
            count_batteries(position) / 3.0, 1.0
        )
        idx += 1
        
        # f[99]: Forcing move ratio
        forcing = count_forcing_moves(position, position.side_to_move)
        total = count_legal_moves(position)
        features[idx] = forcing / max(total, 1) if total > 0 else 0.0
        idx += 1
        
        assert idx == 100
        
        return features
```

---

## V. Multi-Task Loss Function Design

### 5.1 Composite Loss Architecture

```python
class HARENNLoss(nn.Module):
    """Multi-task loss function for HARENN training.
    
    Combines 5 head-specific losses with learned task weights.
    
    Key design decisions:
    1. Eval loss dominates (it's the most important head)
    2. MCS loss uses masked MSE (only for legal moves)
    3. All heads share gradients through backbone → mutual regularization
    4. Task weights are LEARNABLE (uncertainty-based multi-task learning)
    """
    
    def __init__(self):
        super().__init__()
        
        # Learnable task weights (log variance parameterization)
        # σ²_task controls how much each task contributes to total loss
        # Larger σ² → smaller weight (less certain tasks get downweighted)
        self.log_var_eval = nn.Parameter(torch.tensor(0.0))
        self.log_var_tac = nn.Parameter(torch.tensor(0.0))
        self.log_var_mcs = nn.Parameter(torch.tensor(0.0))
        self.log_var_horizon = nn.Parameter(torch.tensor(0.0))
        self.log_var_resolution = nn.Parameter(torch.tensor(0.0))
        
        # Fixed relative importance multipliers
        self.importance = {
            'eval': 1.0,        # Primary task
            'tactical': 0.3,    # Important auxiliary
            'mcs': 0.5,         # Very important for HARE
            'horizon': 0.25,    # Important auxiliary
            'resolution': 0.15, # Least important (for DQRS)
        }
    
    def forward(self, predictions, targets, legal_move_mask=None):
        """Compute composite loss
        
        Args:
            predictions: HARENNOutput from forward pass
            targets: HARENNTargets with all labels
            legal_move_mask: [64, 64] binary mask for legal moves
        
        Returns:
            total_loss, loss_dict (for logging)
        """
        
        loss_dict = {}
        
        # ═══ LOSS 1: EVALUATION ═══
        
        eval_loss = self.eval_loss(
            predictions.eval_score, 
            targets.eval_score,
            targets.game_result
        )
        loss_dict['eval'] = eval_loss.item()
        
        # ═══ LOSS 2: TACTICAL COMPLEXITY ═══
        
        tac_loss = F.mse_loss(
            predictions.tactical_complexity,
            targets.tactical_complexity
        )
        loss_dict['tactical'] = tac_loss.item()
        
        # ═══ LOSS 3: MOVE CRITICALITY SCORES ═══
        
        mcs_loss = self.mcs_loss(
            predictions.mcs_map,
            targets.mcs_map,
            legal_move_mask
        )
        loss_dict['mcs'] = mcs_loss.item()
        
        # ═══ LOSS 4: HORIZON RISK ═══
        
        horizon_loss = F.binary_cross_entropy(
            predictions.horizon_risk,
            targets.horizon_risk
        )
        loss_dict['horizon'] = horizon_loss.item()
        
        # ═══ LOSS 5: RESOLUTION SCORE ═══
        
        resolution_loss = F.mse_loss(
            predictions.resolution_score,
            targets.resolution_score
        )
        loss_dict['resolution'] = resolution_loss.item()
        
        # ═══ COMBINED LOSS (uncertainty-weighted) ═══
        
        # Using Kendall et al. (2018) uncertainty weighting:
        # L_total = Σ (1/(2σ²_i)) · L_i + log(σ²_i)
        
        total_loss = (
            self.weighted_task_loss(eval_loss, self.log_var_eval, 
                                     self.importance['eval']) +
            self.weighted_task_loss(tac_loss, self.log_var_tac,
                                     self.importance['tactical']) +
            self.weighted_task_loss(mcs_loss, self.log_var_mcs,
                                     self.importance['mcs']) +
            self.weighted_task_loss(horizon_loss, self.log_var_horizon,
                                     self.importance['horizon']) +
            self.weighted_task_loss(resolution_loss, self.log_var_resolution,
                                     self.importance['resolution'])
        )
        
        loss_dict['total'] = total_loss.item()
        loss_dict['weights'] = {
            'eval': (1.0 / (2 * torch.exp(self.log_var_eval))).item(),
            'tactical': (1.0 / (2 * torch.exp(self.log_var_tac))).item(),
            'mcs': (1.0 / (2 * torch.exp(self.log_var_mcs))).item(),
            'horizon': (1.0 / (2 * torch.exp(self.log_var_horizon))).item(),
            'resolution': (1.0 / (2 * torch.exp(self.log_var_resolution))).item(),
        }
        
        return total_loss, loss_dict
    
    def weighted_task_loss(self, loss, log_var, importance):
        """Uncertainty-weighted task loss"""
        precision = torch.exp(-log_var)
        return importance * (precision * loss + log_var)
    
    def eval_loss(self, predicted, target_score, game_result):
        """Evaluation loss with game result blending.
        
        λ · MSE(pred, search_score) + (1-λ) · BCE(sigmoid(pred), result)
        """
        
        LAMBDA = 0.75  # Stockfish default
        EVAL_SCALE = 400.0
        
        # MSE against search score
        mse = F.mse_loss(predicted, target_score / EVAL_SCALE)
        
        # BCE against game result
        # Map game result {-1, 0, 1} to {0, 0.5, 1}
        result_target = (game_result + 1.0) / 2.0
        
        pred_prob = torch.sigmoid(predicted * 2.5)  # WDL mapping
        bce = F.binary_cross_entropy(pred_prob, result_target)
        
        return LAMBDA * mse + (1 - LAMBDA) * bce
    
    def mcs_loss(self, predicted_map, target_map, legal_mask):
        """Move Criticality Scores loss.
        
        MASKED MSE: only compute loss for legal moves.
        Illegal move scores (most of the 64×64 map) are ignored.
        
        WEIGHTED: errors on critical moves (high target MCS) 
        are penalized MORE than errors on non-critical moves.
        """
        
        if legal_mask is None:
            # Fallback: treat non-zero targets as legal
            legal_mask = (target_map > 0).float()
        
        # Weight: critical moves have higher weight in loss
        # This ensures network learns to detect critical moves well
        weight_map = 1.0 + target_map * 3.0
        # target=0 → weight=1, target=1 → weight=4
        
        # Masked, weighted MSE
        diff = (predicted_map - target_map) ** 2
        weighted_diff = diff * weight_map * legal_mask
        
        # Normalize by number of legal moves
        num_legal = legal_mask.sum() + 1e-8
        
        return weighted_diff.sum() / num_legal
```

### 5.2 Gradient Flow Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│                     GRADIENT FLOW VISUALIZATION                          │
│                                                                          │
│     Head 1         Head 2         Head 3         Head 4        Head 5    │
│     (Eval)       (Tactical)      (MCS)        (Horizon)    (Resolution) │
│       │              │              │              │             │       │
│       ▼              ▼              ▼              ▼             ▼       │
│     L_eval         L_tac         L_mcs         L_hor         L_res      │
│       │              │              │              │             │       │
│       │              │              │              │             │       │
│       ▼              ▼              ▼              ▼             ▼       │
│   ┌──────────────────────────────────────────────────────────────────┐   │
│   │                  WEIGHTED SUM (uncertainty)                      │   │
│   │  L = Σ (1/2σ²_i) · L_i · w_i + log(σ²_i)                      │   │
│   └───────────────────────────┬──────────────────────────────────────┘   │
│                               │                                          │
│                               ▼ ∂L/∂shared                              │
│   ┌───────────────────────────────────────────────────────────────────┐  │
│   │              SHARED HIDDEN BLOCK (256→128)                        │  │
│   │                                                                   │  │
│   │  Receives gradients from ALL 5 heads simultaneously              │  │
│   │  → Learns features useful for ALL tasks                          │  │
│   │  → MUTUAL REGULARIZATION prevents overfitting to any single task │  │
│   │  → Features become RICHER than single-task training              │  │
│   └───────────────────────────┬───────────────────────────────────────┘  │
│                               │                                          │
│                               ▼ ∂L/∂fusion                              │
│   ┌───────────────────────────────────────────────────────────────────┐  │
│   │              FUSION LAYER (2112→512)                              │  │
│   │                                                                   │  │
│   │  Learns how to combine piece positions with tactical features    │  │
│   │  Gradients from MCS head teach: "these piece configurations      │  │
│   │  make certain moves critical"                                    │  │
│   │  Gradients from horizon head teach: "these configurations        │  │
│   │  are prone to evaluation errors"                                 │  │
│   └──────────┬────────────────────────────────┬──────────────────────┘  │
│              ▼                                ▼                          │
│   ┌──────────────────┐              ┌────────────────────────┐          │
│   │  Feature         │              │  Tactical Encoder      │          │
│   │  Transformer     │              │  (100→64)              │          │
│   │  (45K→1024)      │              │                        │          │
│   │                  │              │  Learns: which tactical │          │
│   │  Learns: piece   │              │  features matter most  │          │
│   │  placement →     │              │  for reduction/horizon │          │
│   │  multi-purpose   │              │  decisions             │          │
│   │  representation  │              └────────────────────────┘          │
│   └──────────────────┘                                                   │
│                                                                          │
│  KEY BENEFIT OF MULTI-TASK LEARNING:                                    │
│                                                                          │
│  The Feature Transformer, which encodes piece placements, receives      │
│  gradient signal from ALL 5 tasks. It learns to encode positions in     │
│  a way that captures:                                                    │
│  - Eval-relevant patterns (from Head 1 → standard)                      │
│  - Tactical complexity patterns (from Head 2 → NEW)                     │
│  - Move importance patterns (from Head 3 → NEW)                         │
│  - Horizon vulnerability patterns (from Head 4 → NEW)                   │
│  - Resolution state (from Head 5 → NEW)                                 │
│                                                                          │
│  This makes the EVAL HEAD ITSELF more accurate!                          │
│  Because the shared features now capture more about the position.       │
│  Multi-task → better eval → DOUBLE benefit.                             │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## VI. Training Pipeline & Infrastructure

### 6.1 Multi-Stage Training Strategy

```python
class HARENNTrainingPipeline:
    """Complete training pipeline for HARENN.
    
    Strategy: STAGED TRAINING
    
    Stage 1: Pre-train eval head only (bootstrap from Stockfish NNUE)
    Stage 2: Freeze FT, train heads 2-5 with auxiliary losses
    Stage 3: Unfreeze all, fine-tune jointly with all 5 losses
    Stage 4: Final fine-tune with hard examples (horizon-prone positions)
    """
    
    def __init__(self, config):
        self.config = config
        self.model = HARENNNetwork()
        self.loss_fn = HARENNLoss()
        
        # Optimizer: different learning rates per component
        self.optimizer = self.create_optimizer()
        
        # Learning rate schedule
        self.scheduler = self.create_scheduler()
    
    def create_optimizer(self):
        """Create optimizer with per-component learning rates"""
        
        param_groups = [
            # Feature Transformer: lowest LR (pre-trained, fragile)
            {
                'params': self.model.feature_transformer.parameters(),
                'lr': self.config.ft_lr,  # 1e-5
                'weight_decay': 1e-6,
            },
            # Tactical Encoder: moderate LR (new component)
            {
                'params': self.model.tactical_encoder.parameters(),
                'lr': self.config.tac_lr,  # 5e-4
                'weight_decay': 1e-5,
            },
            # Fusion + Shared: moderate LR
            {
                'params': list(self.model.fusion.parameters()) +
                          list(self.model.shared_h1.parameters()) +
                          list(self.model.shared_h2.parameters()),
                'lr': self.config.shared_lr,  # 3e-4
                'weight_decay': 1e-5,
            },
            # Eval Head: low LR (pre-trained)
            {
                'params': self.model.eval_head.parameters(),
                'lr': self.config.eval_lr,  # 5e-5
                'weight_decay': 1e-5,
            },
            # Search Heads (2-5): highest LR (training from scratch)
            {
                'params': list(self.model.tactical_head.parameters()) +
                          list(self.model.mcs_head.parameters()) +
                          list(self.model.horizon_head.parameters()) +
                          list(self.model.resolution_head.parameters()),
                'lr': self.config.search_head_lr,  # 1e-3
                'weight_decay': 1e-4,
            },
            # Loss weights (log variances): separate LR
            {
                'params': [self.loss_fn.log_var_eval,
                          self.loss_fn.log_var_tac,
                          self.loss_fn.log_var_mcs,
                          self.loss_fn.log_var_horizon,
                          self.loss_fn.log_var_resolution],
                'lr': self.config.weight_lr,  # 1e-3
                'weight_decay': 0,
            },
        ]
        
        return torch.optim.AdamW(param_groups)
    
    def train_stage_1(self, eval_data, epochs=30):
        """Stage 1: Bootstrap from Stockfish NNUE weights
        
        Load pre-trained Stockfish NNUE weights for Feature Transformer.
        Train eval head only on standard data.
        This gives us a strong starting point for the shared backbone.
        """
        
        print("═══ STAGE 1: Eval Head Bootstrap ═══")
        
        # Load pre-trained weights
        self.load_stockfish_weights()
        
        # Freeze everything except eval head and fusion
        self.freeze_search_heads()
        self.freeze_tactical_encoder()
        
        for epoch in range(epochs):
            epoch_loss = 0
            
            for batch in eval_data.batches(batch_size=16384):
                predictions = self.model.inference_eval_only(
                    batch.white_features,
                    batch.black_features,
                    batch.stm,
                )
                
                loss = self.loss_fn.eval_loss(
                    predictions, batch.eval_score, batch.game_result
                )
                
                loss.backward()
                self.optimizer.step()
                self.optimizer.zero_grad()
                
                epoch_loss += loss.item()
            
            print(f"  Epoch {epoch}: eval_loss = {epoch_loss:.4f}")
        
        print("Stage 1 complete. Eval head calibrated.")
    
    def train_stage_2(self, full_data, epochs=50):
        """Stage 2: Train search heads with frozen FT
        
        Freeze Feature Transformer (preserve eval quality).
        Train heads 2-5 and tactical encoder.
        This teaches the network search-relevant outputs.
        """
        
        print("═══ STAGE 2: Search Heads Training ═══")
        
        self.freeze_feature_transformer()
        self.unfreeze_search_heads()
        self.unfreeze_tactical_encoder()
        
        for epoch in range(epochs):
            metrics = defaultdict(float)
            batch_count = 0
            
            for batch in full_data.batches(batch_size=8192):
                predictions = self.model(
                    batch.white_features,
                    batch.black_features,
                    batch.tactical_features,
                    batch.stm,
                )
                
                total_loss, loss_dict = self.loss_fn(
                    predictions, batch.targets, batch.legal_mask
                )
                
                total_loss.backward()
                
                # Gradient clipping for stability
                torch.nn.utils.clip_grad_norm_(
                    self.model.parameters(), max_norm=1.0
                )
                
                self.optimizer.step()
                self.optimizer.zero_grad()
                
                for k, v in loss_dict.items():
                    if isinstance(v, float):
                        metrics[k] += v
                batch_count += 1
            
            # Log
            for k in metrics:
                metrics[k] /= batch_count
            
            print(f"  Epoch {epoch}: " + 
                  " | ".join(f"{k}={v:.4f}" for k, v in metrics.items()))
            
            self.scheduler.step()
        
        print("Stage 2 complete. Search heads trained.")
    
    def train_stage_3(self, full_data, epochs=100):
        """Stage 3: Joint fine-tuning with ALL heads
        
        Unfreeze everything. Fine-tune with low learning rate.
        This allows Feature Transformer to adapt to multi-task signal.
        """
        
        print("═══ STAGE 3: Joint Fine-Tuning ═══")
        
        self.unfreeze_all()
        
        # Lower learning rates for fine-tuning
        for param_group in self.optimizer.param_groups:
            param_group['lr'] *= 0.3
        
        best_validation_loss = float('inf')
        patience = 10
        no_improve_count = 0
        
        for epoch in range(epochs):
            # Training
            train_metrics = self.train_epoch(full_data.train)
            
            # Validation
            val_metrics = self.validate(full_data.validation)
            
            print(f"  Epoch {epoch}: "
                  f"train={train_metrics['total']:.4f} "
                  f"val={val_metrics['total']:.4f}")
            
            # Early stopping
            if val_metrics['total'] < best_validation_loss:
                best_validation_loss = val_metrics['total']
                self.save_checkpoint('best_model.pt')
                no_improve_count = 0
            else:
                no_improve_count += 1
                if no_improve_count >= patience:
                    print(f"  Early stopping at epoch {epoch}")
                    break
            
            self.scheduler.step()
        
        # Load best model
        self.load_checkpoint('best_model.pt')
        print("Stage 3 complete.")
    
    def train_stage_4(self, hard_data, epochs=30):
        """Stage 4: Fine-tune on hard examples
        
        Focus on positions where horizon effect is most impactful:
        - Tactical positions with hidden resources
        - Positions where LMR causes errors
        - Positions where QSearch is inaccurate
        
        This stage uses a CURATED dataset of hard positions.
        """
        
        print("═══ STAGE 4: Hard Example Fine-Tuning ═══")
        
        # Even lower learning rate
        for param_group in self.optimizer.param_groups:
            param_group['lr'] *= 0.5
        
        # Increase weight of search heads (focus on accuracy)
        self.loss_fn.importance['mcs'] = 0.8
        self.loss_fn.importance['horizon'] = 0.5
        
        for epoch in range(epochs):
            metrics = self.train_epoch(hard_data)
            
            print(f"  Epoch {epoch}: "
                  f"total={metrics['total']:.4f} "
                  f"mcs={metrics['mcs']:.4f} "
                  f"horizon={metrics['horizon']:.4f}")
        
        print("Stage 4 complete. Model optimized for hard positions.")
    
    def train_epoch(self, data):
        """Train one epoch"""
        
        self.model.train()
        metrics = defaultdict(float)
        batch_count = 0
        
        for batch in data.batches(batch_size=8192):
            predictions = self.model(
                batch.white_features,
                batch.black_features,
                batch.tactical_features,
                batch.stm,
            )
            
            total_loss, loss_dict = self.loss_fn(
                predictions, batch.targets, batch.legal_mask
            )
            
            total_loss.backward()
            torch.nn.utils.clip_grad_norm_(
                self.model.parameters(), max_norm=1.0
            )
            self.optimizer.step()
            self.optimizer.zero_grad()
            
            for k, v in loss_dict.items():
                if isinstance(v, float):
                    metrics[k] += v
            batch_count += 1
        
        for k in metrics:
            metrics[k] /= batch_count
        
        return metrics
    
    def generate_hard_examples(self, data_pipeline, num_positions=500000):
        """Generate dataset of hard positions for Stage 4.
        
        "Hard" = positions where horizon effect is measured to be significant.
        """
        
        hard_positions = []
        
        for pos in data_pipeline.random_positions(num_positions * 5):
            # Quick screen: is this position likely horizon-prone?
            
            # Criterion 1: Score changes significantly at deeper search
            shallow = search(pos, depth=8)
            deep = search(pos, depth=13)
            
            score_diff = abs(deep.score - shallow.score)
            
            if score_diff > 40:
                # Significant horizon effect detected!
                sample = data_pipeline.generate_sample(
                    pos, game_result=0
                )
                if sample:
                    hard_positions.append(sample)
            
            # Criterion 2: Best move changes with depth
            if shallow.best_move != deep.best_move:
                sample = data_pipeline.generate_sample(
                    pos, game_result=0
                )
                if sample:
                    hard_positions.append(sample)
            
            if len(hard_positions) >= num_positions:
                break
        
        print(f"Generated {len(hard_positions)} hard examples")
        return HARENNDataset(hard_positions)
```

### 6.2 Curriculum Learning Schedule

```
┌──────────────────────────────────────────────────────────────────────┐
│                  HARENN TRAINING CURRICULUM                           │
│                                                                      │
│  Stage 1 (Bootstrap): 2-3 days on 8× A100                          │
│  ├── Data: 1B positions (standard NNUE data)                        │
│  ├── Task: Eval only                                                │
│  ├── LR: 1e-3 → 1e-5 (cosine decay)                               │
│  └── Goal: Eval accuracy ≈ Stockfish NNUE                          │
│                                                                      │
│  Stage 2 (Search Heads): 4-5 days on 8× A100                       │
│  ├── Data: 200M positions (with all 5 labels)                       │
│  ├── Task: All 5 heads, FT frozen                                   │
│  ├── LR: 1e-3 → 1e-4                                               │
│  ├── Curriculum: start with τ and rs (easier), add MCS and ρ later │
│  └── Goal: Search head accuracy > 80%                               │
│                                                                      │
│  Stage 3 (Joint Fine-tune): 5-7 days on 8× A100                    │
│  ├── Data: 500M positions (balanced difficulty)                     │
│  ├── Task: All heads, all parameters                                │
│  ├── LR: 3e-4 → 1e-5                                               │
│  ├── Early stopping on validation loss                              │
│  └── Goal: Eval + search accuracy both improved                     │
│                                                                      │
│  Stage 4 (Hard Examples): 2-3 days on 8× A100                      │
│  ├── Data: 50M hard positions (horizon-prone)                       │
│  ├── Task: Focus on MCS and horizon heads                           │
│  ├── LR: 1e-4 → 1e-6                                               │
│  └── Goal: Horizon effect detection > 90%                           │
│                                                                      │
│  Total training time: ~14-18 days on 8× A100                       │
│  (Stockfish NNUE: ~5-7 days — HARENN is ~2.5× more expensive)      │
│                                                                      │
│  Total training data:                                                │
│  Stage 1: 1B × 1 label = 1B label-positions                        │
│  Stage 2-3: 700M × 5 labels = 3.5B label-positions                 │
│  Stage 4: 50M × 5 labels = 250M label-positions                    │
│  TOTAL: ~4.75B label-positions                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## VII. HARE Search Integration

### 7.1 HARENN Replaces HARE Components

```
┌────────────────────────────────────────────────────────────────────────┐
│              HARE COMPONENT → HARENN HEAD MAPPING                      │
│                                                                        │
│  HARE Component                │ HARENN Replacement    │ Speed Gain   │
│  ──────────────────────────────┼───────────────────────┼──────────────│
│  TacticalDensityMap            │ τ (Head 2) ≈ global   │ 3-5μs → 0μs │
│  ThreatLandscapeScanner        │ τ + MCS (Heads 2+3)  │ 3-8μs → 0μs │
│  PositionalTensionGauge        │ τ (Head 2) direct     │ 1-2μs → 0μs │
│  MoveTacticalRelevanceScorer   │ MCS (Head 3) lookup   │ 2-3μs → ~0  │
│  MoveThreatIntersection        │ MCS (Head 3) implicit │ 1-3μs → 0μs │
│  DefensiveRelevanceAssessment  │ MCS (Head 3) implicit │ 1-2μs → ~0  │
│  HorizonProximityEstimator     │ ρ (Head 4) direct     │ 1-2μs → 0μs │
│  ──────────────────────────────┼───────────────────────┼──────────────│
│  HARENN inference (all heads)  │ ONE forward pass      │ +1.5-3.0μs  │
│  ──────────────────────────────┼───────────────────────┼──────────────│
│  NET per-node cost             │                       │              │
│    Hand-crafted HARE           │ 12-25 μs              │              │
│    HARENN-powered HARE         │ 1.5-3.0 μs            │ 75-88% saved│
│                                │ + MCS lookups: ~0.01μs│              │
│                                │                        │              │
│  COMPONENTS THAT REMAIN:       │                        │              │
│  CascadeBudgetLimiter          │ Kept (cheap, reliable) │ 0.05μs      │
│  ReductionHealthMonitor        │ Kept (runtime only)    │ 0.05μs      │
│  SuspiciousFailLowVerifier     │ Enhanced by ρ signal   │ saved 50%   │
│  AntiHistoryCorrection         │ Kept (independent)     │ 0.05μs      │
│  CrossNodeIntelligence         │ Kept (independent)     │ 0.1μs       │
│  HorizonProbe search           │ Guided by ρ (fewer)   │ saved 40-60%│
└────────────────────────────────────────────────────────────────────────┘
```

### 7.2 Integrated Search Loop

```python
def search_with_harenn(position, depth, alpha, beta, ply, 
                        is_pv, search_state):
    """Main search function with HARENN-powered HARE"""
    
    # ═══ STANDARD NODE PROCESSING ═══
    
    tt_entry = tt_probe(position)
    # ... null move, razoring, futility, etc. ...
    
    # ═══ HARENN INFERENCE ═══
    # One forward pass gives us everything HARE needs
    
    harenn_output = None
    
    if depth >= 4:  # Only at meaningful depth
        # Extract tactical features (cheap: ~0.3μs)
        tac_features = extract_tactical_features(position)
        
        # Run HARENN (one forward pass: ~1.5-3μs)
        harenn_output = harenn_model.inference_hare(
            search_state.accumulator_white,
            search_state.accumulator_black,
            tac_features,
            position.side_to_move,
        )
        
        # Now we have:
        # harenn_output.eval_score           → position evaluation
        # harenn_output.tactical_complexity  → τ (tension gauge replacement)
        # harenn_output.mcs_map             → per-move criticality (MRC replacement)
        # harenn_output.horizon_risk        → ρ (horizon proximity replacement)
        # harenn_output.resolution_score    → rs (for DQRS)
    
    elif depth >= 1:
        # At shallow depth: only eval, use simplified HARE
        harenn_output = harenn_model.inference_eval_only(
            search_state.accumulator_white,
            search_state.accumulator_black,
            position.side_to_move,
        )
    
    # ═══ USE EVAL FROM HARENN ═══
    
    if harenn_output:
        static_eval = harenn_output.eval_score
    else:
        static_eval = nnue_eval(position)  # Fallback
    
    # ═══ MOVE GENERATION & ORDERING ═══
    
    moves = generate_and_order_moves(position, search_state)
    
    # Initialize monitors
    health_monitor = ReductionHealthMonitor()
    
    best_score = -INFINITY
    best_move = None
    moves_searched = 0
    
    for i, move in enumerate(moves):
        search_state.move_index = i
        
        # ═══ STANDARD PRUNING ═══
        
        if should_prune(position, move, depth, alpha, beta, 
                        search_state):
            continue
        
        # ═══ HARENN-POWERED REDUCTION ═══
        
        reduction = 0
        
        if i >= 3 and depth >= 3 and not in_check(position):
            
            if harenn_output and depth >= 4:
                # FULL HARENN-POWERED REDUCTION
                reduction = harenn_compute_reduction(
                    position, move, depth, harenn_output,
                    search_state, cascade_estimator,
                    health_monitor
                )
            else:
                # Fallback to simple LMR at shallow depth
                reduction = simple_lmr(depth, i)
        
        # ═══ SEARCH WITH REDUCTION ═══
        
        cascade_estimator.push_reduction(reduction, depth)
        position.make_move(move)
        
        if i == 0:
            score = -search_with_harenn(
                position, depth - 1, -beta, -alpha, ply + 1,
                is_pv, search_state
            )
        else:
            # Zero-window with reduction
            score = -search_with_harenn(
                position, depth - 1 - reduction, -(alpha + 1), -alpha,
                ply + 1, False, search_state
            )
            
            was_re_searched = False
            
            # Fail-high re-search
            if score > alpha and reduction > 0:
                score = -search_with_harenn(
                    position, depth - 1, -(alpha + 1), -alpha,
                    ply + 1, False, search_state
                )
                was_re_searched = True
            
            # HARENN-GUIDED SUSPICIOUS FAIL-LOW VERIFICATION
            elif (score <= alpha and reduction >= 2 and 
                  harenn_output is not None):
                
                mcs = harenn_output.mcs_map[move.from_sq][move.to_sq]
                rho = harenn_output.horizon_risk
                
                # If move has high MCS AND position has high horizon risk
                # → suspicious fail-low → verify
                if mcs > 0.6 and rho > 0.4:
                    verify_depth = depth - 1 - max(0, reduction - 2)
                    verify_score = -search_with_harenn(
                        position, verify_depth, -(alpha + 1), -alpha,
                        ply + 1, False, search_state
                    )
                    if verify_score > score:
                        score = verify_score
                        was_re_searched = True
            
            # PV re-search
            if score > alpha and score < beta and is_pv:
                score = -search_with_harenn(
                    position, depth - 1, -beta, -alpha,
                    ply + 1, True, search_state
                )
            
            # Record for health monitoring
            health_monitor.record_move_result(
                i, score, reduction, was_re_searched,
                score > best_score
            )
        
        position.unmake_move(move)
        cascade_estimator.pop_reduction()
        
        if score > best_score:
            best_score = score
            best_move = move
        if score > alpha:
            alpha = score
        if score >= beta:
            update_history(move, depth, search_state)
            break
    
    return best_score


def harenn_compute_reduction(position, move, depth, harenn_output,
                              search_state, cascade_estimator,
                              health_monitor):
    """Compute reduction using HARENN outputs"""
    
    # ═══ IRREDUCIBLE CHECK ═══
    
    if is_irreducible(position, move, search_state):
        return 0
    
    # ═══ BASE REDUCTION ═══
    
    base_R = base_lmr_formula(depth, search_state.move_index)
    
    # ═══ MOVE CRITICALITY (from MCS map) ═══
    # This REPLACES all of HARE's per-move analysis
    
    mcs = harenn_output.mcs_map[move.from_sq][move.to_sq]
    # mcs high → critical move → reduce less
    # mcs low → safe to reduce
    
    mrc = 1.0 - mcs  # Move Reduction Confidence
    confidence_R = base_R * mrc
    
    # ═══ TACTICAL COMPLEXITY (from τ) ═══
    # This REPLACES TacticalDensityMap + TensionGauge + ThreatLandscape
    
    tau = harenn_output.tactical_complexity
    
    # High complexity → reduce less (global factor)
    if tau > 0.8:
        global_factor = 0.3
    elif tau > 0.6:
        global_factor = 0.5
    elif tau > 0.4:
        global_factor = 0.75
    elif tau > 0.25:
        global_factor = 1.0
    else:
        global_factor = 1.2  # Very quiet → reduce more
    
    tactical_R = confidence_R * global_factor
    
    # ═══ HORIZON RISK (from ρ) ═══
    # This REPLACES HorizonProximityEstimator + part of HorizonProbe
    
    rho = harenn_output.horizon_risk
    
    # High horizon risk AND critical move → don't reduce!
    if rho > 0.6 and mcs > 0.5:
        # Dangerous combination: likely horizon effect if we reduce
        horizon_R = tactical_R * 0.3
    elif rho > 0.4:
        horizon_R = tactical_R * (1.0 - rho * 0.5)
    else:
        horizon_R = tactical_R
    
    # ═══ CASCADE BUDGET ═══
    
    cascade_R = cascade_estimator.limit_reduction(
        int(horizon_R), search_state.root_depth
    )
    
    # ═══ HEALTH MONITOR ADJUSTMENT ═══
    
    health_adj = health_monitor.get_adjustment(
        search_state.move_index, mrc
    )
    final_R = max(0, cascade_R + int(health_adj))
    
    # ═══ STANDARD ADJUSTMENTS ═══
    
    if search_state.in_check:
        final_R = max(0, final_R - 1)
    if search_state.is_pv_node:
        final_R = max(0, final_R - 1)
    if search_state.improving:
        final_R = max(0, final_R - 1)
    
    # ═══ HARENN-SPECIFIC: HORIZON PROBE GATING ═══
    # Only run expensive horizon probe if HARENN says it's risky
    
    if final_R >= 2 and rho > 0.5 and mcs > 0.4:
        # HARENN thinks this is risky → do a horizon probe
        probe_result = horizon_probe.probe(
            position, move, depth, final_R, alpha, beta
        )
        if not probe_result.proceed_with_reduction:
            final_R = probe_result.recommended_R
    
    # Clamp
    return clamp(final_R, 0, depth - 2)
```

### 7.3 DQRS Integration (Cross-Architecture Benefit)

```python
def dqrs_with_harenn(position, alpha, beta, depth_budget):
    """DQRS using HARENN's resolution score"""
    
    # Use HARENN's resolution score instead of hand-crafted assessment
    if hasattr(position, '_harenn_output') and position._harenn_output:
        rs = position._harenn_output.resolution_score
    else:
        # Compute fresh
        tac_features = extract_tactical_features(position)
        harenn_out = harenn_model.inference_hare(
            position.acc_white, position.acc_black,
            tac_features, position.stm
        )
        rs = harenn_out.resolution_score
    
    # Resolution score directly determines QSearch behavior
    
    if rs > 0.95:
        # Fully resolved → just return eval
        return harenn_out.eval_score
    
    if rs > 0.8:
        # Mostly resolved → minimal QSearch
        return quick_qsearch(position, alpha, beta, max_depth=2)
    
    if rs > 0.5:
        # Partially resolved → moderate QSearch
        return dqrs_standard(position, alpha, beta, 
                             depth_budget=min(depth_budget, 5))
    
    # Low resolution → full DQRS
    return dqrs_standard(position, alpha, beta, depth_budget)
```

---

## VIII. Ước Tính Ảnh Hưởng Tổng Hợp

```
┌──────────────────────────────────────────────┬────────────┬──────────────┐
│ Improvement Source                            │ Elo Est.   │ Confidence   │
├──────────────────────────────────────────────┼────────────┼──────────────┤
│ A. EVAL IMPROVEMENT (multi-task backbone)    │ +10-25     │ ★★★★ High    │
│    Multi-task gradients enrich FT features    │ +5-12      │              │
│    Tactical features supplement HalfKA        │ +3-8       │              │
│    Hard example fine-tuning                   │ +2-5       │              │
│                                              │            │              │
│ B. HARE REPLACEMENT (neural > heuristic)     │ +15-35     │ ★★★★ High    │
│    MCS map (learned move criticality)         │ +8-18      │              │
│    τ (learned tactical complexity)            │ +4-10      │              │
│    ρ (learned horizon risk)                   │ +3-8       │              │
│                                              │            │              │
│ C. SPEED IMPROVEMENT (cheaper inference)     │ +8-20      │ ★★★★ High    │
│    Replace 12-25μs HARE with 1.5-3μs HARENN  │ +5-12      │              │
│    Fewer horizon probes needed (ρ-guided)     │ +2-5       │              │
│    Faster fail-low verification (MCS-guided)  │ +1-3       │              │
│                                              │            │              │
│ D. ACCURACY IMPROVEMENT                      │ +10-25     │ ★★★ Medium   │
│    MCS more accurate than hand-crafted MRC    │ +5-12      │              │
│    τ more accurate than tension gauge         │ +3-8       │              │
│    ρ catches horizon effects hand-craft misses│ +3-6       │              │
│                                              │            │              │
│ E. DQRS INTEGRATION BONUS                    │ +5-12      │ ★★★ Medium   │
│    Resolution score guides QSearch depth       │ +3-7       │              │
│    Shared eval quality from multi-task        │ +2-5       │              │
│                                              │            │              │
│ F. SELF-IMPROVEMENT POTENTIAL                 │ +5-15      │ ★★ Medium    │
│    Retrain with more data → better decisions  │ +3-8       │              │
│    Fine-tune on tournament positions          │ +2-7       │              │
├──────────────────────────────────────────────┼────────────┼──────────────┤
│ TOTAL (with overlap)                         │ +40-95     │              │
│ After overhead/risk deduction                │ +30-75     │              │
│ Conservative estimate                        │ +25-55     │              │
│ Realistic center                             │ +35-60     │              │
├──────────────────────────────────────────────┼────────────┼──────────────┤
│ COMPARED TO:                                 │            │              │
│   HARE alone (hand-crafted)                  │ +25-65     │              │
│   Standard NNUE alone                        │ baseline   │              │
│   HARENN vs HARE (HARENN additional gain)     │ +10-30     │              │
│   HARENN vs Standard NNUE                    │ +35-60     │              │
└──────────────────────────────────────────────┴────────────┴──────────────┘

By position type:
┌──────────────────────────────┬────────────┬──────────────────────────────┐
│ Position Type                │ Improvement│ Key Contributing Factors      │
│                              │ vs StdNNUE │                              │
├──────────────────────────────┼────────────┼──────────────────────────────┤
│ Sharp tactical               │ +45-80 Elo │ MCS accuracy, ρ detection,   │
│                              │            │ τ-guided global reduction    │
│ Quiet positional             │ +10-20 Elo │ Increased reduction (τ low), │
│                              │            │ speed from reduced overhead  │
│ King attack / Sacrifice      │ +40-70 Elo │ MCS detects defensive moves, │
│                              │            │ ρ prevents horizon errors    │
│ Endgame (tactical)           │ +30-55 Elo │ MCS for pawn pushes, rs for  │
│                              │            │ QSearch depth, eval quality  │
│ Horizon-effect-prone         │ +50-90 Elo │ ρ detection + MCS + τ all    │
│                              │            │ synergize against horizon    │
│ Time pressure                │ +25-45 Elo │ Faster per-node (1.5μs vs   │
│                              │            │ 15μs), fewer re-searches    │
└──────────────────────────────┴────────────┴──────────────────────────────┘
```

---

## IX. Lộ Trình Triển Khai

```
Phase 1 (Month 1-3): Network Architecture & Data Pipeline
├── Design and implement HARENNNetwork (PyTorch)
├── Implement FeatureExtractor (100-dim tactical features)
├── Implement EvalLabelGenerator (standard)
├── Implement TacticalComplexityLabelGenerator
├── Implement ResolutionLabelGenerator
├── Test: network forward/backward pass, quantization
└── Deliverable: Working training framework

Phase 2 (Month 4-6): MCS & Horizon Labels + Stage 1-2 Training
├── Implement MCSLabelGenerator (most complex)
├── Implement HorizonRiskLabelGenerator
├── Generate training data (200M positions, 5 labels each)
├── Train Stage 1 (eval bootstrap from Stockfish)
├── Train Stage 2 (search heads with frozen FT)
├── Evaluate: head accuracy on validation set
└── Target: τ accuracy >80%, MCS accuracy >70%

Phase 3 (Month 7-9): Joint Training & Integration
├── Train Stage 3 (joint fine-tuning)
├── Train Stage 4 (hard examples)
├── Implement quantization pipeline (int8/int16)
├── Implement HARENN inference in C++ engine
├── Integrate with HARE search loop
├── Test: NPS, accuracy, Elo in self-play
└── Target: +15-30 Elo over standard NNUE, faster than hand-crafted HARE

Phase 4 (Month 10-12): Optimization & Scaling
├── SIMD optimization (AVX-512) for HARENN inference
├── Accumulator incremental update optimization
├── MCS map caching and sharing between nodes
├── Generate more training data (1B positions)
├── Retrain with larger dataset
├── Cross-component integration (DQRS, AAW)
├── Extensive self-play testing (100K+ games)
└── Target: +25-45 Elo, production-ready

Phase 5 (Month 13-15): Advanced Training & Refinement
├── Curriculum learning optimization
├── Data augmentation (board flipping, rotation)
├── Knowledge distillation from stronger model
├── Opponent-adaptive fine-tuning (optional)
├── Tournament testing (CCRL/TCEC conditions)
├── Parameter freeze for production
└── Target: +30-55 Elo (final, stable)

Phase 6 (Month 16-18): Research Extensions
├── Attention mechanism for MCS head (better accuracy)
├── Temporal features (game history for MCS)
├── Online learning during game (optional)
├── Architecture search for optimal head sizes
├── Cross-architecture transfer to UPAD, HAMO
└── Target: +35-60 Elo (research frontier)
```

---

## X. So Sánh Tổng Hợp

```
┌──────────────────────────┬──────────────┬──────────────┬──────────────────┐
│ Aspect                   │ Stockfish    │ HARE         │ HARENN           │
│                          │ NNUE+LMR     │ (hand-craft) │ (neural+HARE)    │
├──────────────────────────┼──────────────┼──────────────┼──────────────────┤
│ Eval function            │ NNUE 1-head  │ NNUE 1-head  │ NNUE 5-head      │
│ (what network outputs)   │ (eval only)  │ (eval only)  │ (eval+τ+MCS+ρ+rs)│
│                          │              │              │                  │
│ Reduction decision       │ Fixed formula│ Hand-crafted │ LEARNED from     │
│ basis                    │ + adjustments│ heuristics   │ millions of      │
│                          │              │ (7 modules)  │ positions        │
│                          │              │              │                  │
│ Per-move analysis cost   │ ~0.1μs       │ 4-10μs       │ ~0.01μs (lookup) │
│                          │ (table+adj)  │ (7 analyses) │ (MCS map)        │
│                          │              │              │                  │
│ Per-node overhead        │ ~0.5μs       │ 12-25μs      │ 1.5-3.0μs       │
│                          │              │              │                  │
│ Horizon effect handling  │ None         │ Multi-layer  │ Learned ρ head   │
│                          │              │ heuristic    │ + guided probes  │
│                          │              │              │                  │
│ Accuracy of reduction    │ ~55-65%      │ ~70-80%      │ ~80-90%          │
│ decisions                │              │              │ (estimated)      │
│                          │              │              │                  │
│ Self-improving?          │ No           │ No           │ YES (retrain)    │
│                          │              │              │                  │
│ Net file size            │ ~40MB        │ ~40MB+code   │ ~48MB            │
│                          │              │              │                  │
│ Training cost            │ ~5-7 days    │ N/A (coded)  │ ~14-18 days      │
│                          │              │              │                  │
│ Elo vs Stockfish NNUE    │ baseline     │ +25-65       │ +35-60           │
│                          │              │ (but slower) │ (AND faster)     │
│                          │              │              │                  │
│ NPS impact               │ baseline     │ -15-30%      │ -5-10%           │
│                          │              │              │ (minimal impact) │
└──────────────────────────┴──────────────┴──────────────┴──────────────────┘
```

---

HARENN chuyển đổi mối quan hệ giữa neural network và search từ **"strangers sharing an engine"** thành **"unified intelligence system"** — bằng cách dạy CÙNG MỘT mạng neural đồng thời **đánh giá thế cờ** (eval head), **hiểu mức độ chiến thuật** (tactical complexity head), **biết nước nào quan trọng** (move criticality scores — toàn bộ 64×64 map trong MỘT lần inference), **dự đoán rủi ro đường chân trời** (horizon risk head), và **đánh giá mức giải quyết** (resolution score head) — tất cả chia sẻ CÙNG backbone features, huấn luyện bằng multi-task learning với uncertainty-weighted losses, và thay thế ~80% chi phí tính toán của HARE hand-crafted trong khi đạt độ chính xác cao hơn 10-20%.