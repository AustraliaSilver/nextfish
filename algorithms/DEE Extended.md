

# Kiến Trúc Mở Rộng cho Dynamic Exchange Evaluation: DEE-X (DEE Extended)

---

## I. Nhắc Lại DEE Gốc & Xác Định Điểm Mở Rộng

### 1.1 DEE Gốc — Tóm Tắt

```
DEE gốc gồm 5 module:

1. Exchange Graph Builder (EGB)
   - Xây đồ thị trao đổi toàn cục
   - Trọng số đa chiều: material, structural, dynamic, temporal

2. Positional Aftermath Evaluator (PAE)
   - Đánh giá hậu quả vị trí sau trao đổi
   - Pawn structure, piece activity, king safety, phase transition

3. Temporal Decay Module (TDM)
   - Giá trị trao đổi theo thời gian
   - Khi nào nên trao đổi vs giữ tension

4. Cross-Cluster Interaction Matrix (CCIM)
   - Mối quan hệ giữa clusters trao đổi
   - Piece diversion, tactical connection, tempo relationship

5. Threat-As-Asset Quantifier (TAQ)
   - Giá trị mối đe dọa trao đổi
   - Threat often > execution (Nimzowitsch)
```

### 1.2 Phân Tích Hạn Chế Của DEE Gốc

```
LIMITATIONS IDENTIFIED IN PRACTICE:

┌──────────────────────────────────────────────────────────────────────┐
│                   DEE ORIGINAL LIMITATIONS                           │
│                                                                      │
│  L1: STATIC EXCHANGE GRAPH                                          │
│  ──────────────────────────                                         │
│  EGB builds graph AT CURRENT POSITION only                          │
│  Doesn't consider how graph EVOLVES over next 3-5 moves            │
│  Example: Currently no exchange possible on e5                      │
│  But after Nd4, e5 becomes heavily contested                        │
│  → DEE misses EMERGING exchange patterns                            │
│                                                                      │
│  L2: ISOLATED AFTERMATH EVALUATION                                  │
│  ─────────────────────────────────                                  │
│  PAE evaluates aftermath of SINGLE exchange sequence                │
│  Doesn't evaluate aftermath of COMBINED exchanges                   │
│  Example: BxN on d5 + RxR on d1 together = different aftermath     │
│  than either exchange alone                                          │
│  → Missing synergistic/conflicting aftermath effects                │
│                                                                      │
│  L3: LINEAR TEMPORAL MODEL                                          │
│  ────────────────────────────                                       │
│  TDM uses exponential decay: simple, but misses                     │
│  - Phase-dependent timing (exchange right before/after castling)    │
│  - Trigger-based timing (exchange when opponent commits to plan)    │
│  - Conditional timing (exchange IF opponent does X)                  │
│  → Temporal model too simplistic for real chess dynamics             │
│                                                                      │
│  L4: PAIRWISE CLUSTER INTERACTION                                   │
│  ────────────────────────────────                                   │
│  CCIM only models PAIRWISE interactions (cluster i → cluster j)    │
│  Misses 3-way+ interactions                                         │
│  Example: Sacrificing on h7 + exchanging on d5 + opening e-file    │
│  = three clusters interacting simultaneously                        │
│  → Higher-order interactions invisible                               │
│                                                                      │
│  L5: BINARY THREAT MODEL                                            │
│  ────────────────────────                                           │
│  TAQ: threat exists or doesn't                                      │
│  Misses: evolving threats, conditional threats, bluffs              │
│  Example: Knight threatens fork on c7                                │
│  But ONLY if opponent castles queenside                              │
│  → Conditional threat value not captured                             │
│                                                                      │
│  L6: NO OPPONENT MODELING                                           │
│  ─────────────────────────                                          │
│  DEE evaluates exchanges objectively                                │
│  Doesn't consider what opponent WANTS to exchange                   │
│  Example: Opponent desperately wants to exchange queens             │
│  → Our queen is more valuable than objective eval suggests          │
│  → Missing exchange negotiation dynamics                             │
│                                                                      │
│  L7: NO PLAN INTEGRATION                                            │
│  ────────────────────────                                           │
│  Exchange evaluation disconnected from strategic plans              │
│  Example: Planning kingside attack → exchanging dark bishop = bad   │
│  Even if exchange is "objectively equal"                             │
│  → Exchange decisions not aligned with strategic goals               │
│                                                                      │
│  L8: NO MATERIAL TRANSFORMATION AWARENESS                          │
│  ─────────────────────────────────────────                          │
│  DEE evaluates exchanges in current material context                │
│  Doesn't project how exchanges change endgame characteristics       │
│  Example: Trading into R+B vs R+N                                   │
│  Value depends heavily on pawn structure that exists AFTER trades    │
│  → Missing deep imbalance evaluation                                │
│                                                                      │
│  L9: COMPUTATIONAL BOTTLENECK                                       │
│  ────────────────────────────                                       │
│  Full DEE too expensive for every node                              │
│  Need graduated computation model                                   │
│  → Performance/quality tradeoff not optimized                        │
│                                                                      │
│  L10: NO LEARNING FROM GAMES                                        │
│  ────────────────────────────                                       │
│  DEE parameters fixed, don't improve with experience               │
│  → Missing self-improvement loop                                    │
└──────────────────────────────────────────────────────────────────────┘
```

### 1.3 DEE-X Scope — Cái Gì Mở Rộng

```
DEE-X adds 8 new modules addressing ALL 10 limitations:

┌──────────────────────────────────────────────────────────────────────┐
│                    DEE-X MODULE MAP                                   │
│                                                                      │
│  ┌─── EXISTING (Enhanced) ───┐   ┌─── NEW Modules ─────────────┐   │
│  │                           │   │                               │   │
│  │  EGB → EGB-E              │   │  M1: Exchange Horizon        │   │
│  │  (Evolutionary Graph)     │   │      Projector (EHP)         │   │
│  │                           │   │                               │   │
│  │  PAE → PAE-S              │   │  M2: Synergistic Aftermath   │   │
│  │  (Synergistic Aftermath)  │   │      Evaluator (SAE)         │   │
│  │                           │   │                               │   │
│  │  TDM → TDM-C              │   │  M3: Conditional Temporal    │   │
│  │  (Conditional Temporal)   │   │      Engine (CTE)            │   │
│  │                           │   │                               │   │
│  │  CCIM → CCIM-H            │   │  M4: Higher-Order           │   │
│  │  (Higher-Order)           │   │      Interaction Tensor(HOIT)│   │
│  │                           │   │                               │   │
│  │  TAQ → TAQ-E              │   │  M5: Threat Evolution       │   │
│  │  (Evolved Threats)        │   │      Dynamics (TED)          │   │
│  │                           │   │                               │   │
│  │                           │   │  M6: Exchange Negotiation    │   │
│  │                           │   │      Model (ENM)             │   │
│  │                           │   │                               │   │
│  │                           │   │  M7: Strategic Exchange      │   │
│  │                           │   │      Alignment (SEA)         │   │
│  │                           │   │                               │   │
│  │                           │   │  M8: Material Transform     │   │
│  │                           │   │      Projector (MTP)         │   │
│  └───────────────────────────┘   └───────────────────────────────┘   │
│                                                                      │
│  ┌─── INFRASTRUCTURE ────────────────────────────────────────────┐   │
│  │                                                               │   │
│  │  I1: Graduated Computation Controller (GCC)                   │   │
│  │  I2: DEE-X Learning Engine (DLE)                              │   │
│  │  I3: DEE-X Integration Hub (DIH)                              │   │
│  └───────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────┘
```

---

## II. Kiến Trúc Tổng Thể DEE-X

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                           DEE-X ARCHITECTURE                                  │
│                  Dynamic Exchange Evaluation — Extended                        │
│                                                                               │
│  ═══════════════════════════════════════════════════════════════════════════  │
│  ║                        PERCEPTION LAYER                                ║  │
│  ║  "How does the exchange landscape LOOK?"                               ║  │
│  ║                                                                        ║  │
│  ║  ┌─────────────┐  ┌─────────────┐  ┌──────────────────────────────┐  ║  │
│  ║  │  EGB-E      │  │  EHP        │  │  TED                        │  ║  │
│  ║  │  Evolutionary│  │  Exchange   │  │  Threat Evolution           │  ║  │
│  ║  │  Exchange   │  │  Horizon    │  │  Dynamics                   │  ║  │
│  ║  │  Graph      │  │  Projector  │  │                             │  ║  │
│  ║  └──────┬──────┘  └──────┬──────┘  └──────────────┬──────────────┘  ║  │
│  ║         └────────┬───────┴────────────────────────┘                 ║  │
│  ═══════════════════╪═════════════════════════════════════════════════  │
│                     ▼                                                     │
│  ═══════════════════════════════════════════════════════════════════════  │
│  ║                        ANALYSIS LAYER                              ║  │
│  ║  "What HAPPENS if we exchange?"                                    ║  │
│  ║                                                                    ║  │
│  ║  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌──────────┐║  │
│  ║  │  PAE-S      │  │  SAE        │  │  HOIT       │  │  MTP     │║  │
│  ║  │  Positional │  │  Synergistic│  │  Higher     │  │  Material│║  │
│  ║  │  Aftermath  │  │  Aftermath  │  │  Order      │  │  Trans-  │║  │
│  ║  │  (enhanced) │  │  Evaluator  │  │  Interaction│  │  form    │║  │
│  ║  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └────┬─────┘║  │
│  ║         └────────┬───────┴────────┬───────┴──────────────┘      ║  │
│  ═══════════════════╪════════════════╪═════════════════════════════  │
│                     ▼                ▼                                 │
│  ═══════════════════════════════════════════════════════════════════  │
│  ║                        STRATEGY LAYER                            ║  │
│  ║  "WHEN and WHY should we exchange?"                              ║  │
│  ║                                                                   ║  │
│  ║  ┌─────────────┐  ┌─────────────┐  ┌──────────────────────────┐ ║  │
│  ║  │  CTE        │  │  ENM        │  │  SEA                     │ ║  │
│  ║  │  Conditional│  │  Exchange   │  │  Strategic Exchange      │ ║  │
│  ║  │  Temporal   │  │  Negotiation│  │  Alignment               │ ║  │
│  ║  │  Engine     │  │  Model      │  │                          │ ║  │
│  ║  └──────┬──────┘  └──────┬──────┘  └──────────────┬───────────┘ ║  │
│  ║         └────────┬───────┴──────────────────┬─────┘             ║  │
│  ═══════════════════╪══════════════════════════╪═══════════════════  │
│                     ▼                          ▼                      │
│  ═══════════════════════════════════════════════════════════════════  │
│  ║                        INTEGRATION LAYER                        ║  │
│  ║  "How does it all FIT TOGETHER?"                                ║  │
│  ║                                                                  ║  │
│  ║  ┌─────────────────────────────────────────────────────────────┐║  │
│  ║  │                DEE-X Integration Hub (DIH)                  │║  │
│  ║  │  ┌───────────────┐  ┌──────────────┐  ┌─────────────────┐ │║  │
│  ║  │  │ Graduated     │  │ Learning     │  │ Score           │ │║  │
│  ║  │  │ Computation   │  │ Engine       │  │ Aggregation     │ │║  │
│  ║  │  │ Controller    │  │ (DLE)        │  │                 │ │║  │
│  ║  │  └───────────────┘  └──────────────┘  └─────────────────┘ │║  │
│  ║  └─────────────────────────────────────────────────────────────┘║  │
│  ═══════════════════════════════════════════════════════════════════  │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## III. Perception Layer — Nhìn Thấy Bức Tranh Trao Đổi

### 3.1 EGB-E: Evolutionary Exchange Graph

```python
class EvolutionaryExchangeGraph:
    """Đồ thị trao đổi TIẾN HÓA — không chỉ snapshot hiện tại
    mà còn dự đoán đồ thị sẽ thay đổi thế nào qua các nước đi
    
    Giải quyết L1: Static Exchange Graph
    
    Key insight: Exchange landscape THAY ĐỔI mỗi nước đi
    Quân di chuyển → ô mới bị/hết contested
    → Cần model SỰ TIẾN HÓA, không chỉ trạng thái
    """
    
    def __init__(self):
        self.current_graph = None     # Đồ thị hiện tại (DEE gốc)
        self.projected_graphs = []    # Đồ thị dự kiến 1-3 nước tới
        self.evolution_trajectory = None  # Quỹ đạo tiến hóa
    
    def build(self, position, depth=3):
        """Xây đồ thị tiến hóa"""
        
        # ═══ PHASE 1: CURRENT GRAPH (DEE gốc) ═══
        self.current_graph = self.build_current_graph(position)
        
        # ═══ PHASE 2: PROJECTED GRAPHS ═══
        # Dự đoán đồ thị trao đổi sau 1, 2, 3 nước
        
        self.projected_graphs = []
        
        for horizon in range(1, depth + 1):
            projected = self.project_graph(position, horizon)
            self.projected_graphs.append(projected)
        
        # ═══ PHASE 3: EVOLUTION TRAJECTORY ═══
        # Phân tích XU HƯỚNG thay đổi đồ thị
        
        self.evolution_trajectory = self.analyze_trajectory()
        
        return self
    
    def project_graph(self, position, horizon):
        """Dự đoán exchange graph sau 'horizon' nước
        
        Không search thực sự — sử dụng heuristics:
        - Likely moves (top-ordered moves)
        - Piece mobility projections
        - Pawn structure evolution
        """
        
        projected = ProjectedExchangeGraph(horizon=horizon)
        
        # Dự đoán nước đi khả năng cao nhất
        likely_moves = get_top_likely_moves(position, n=3)
        
        for move_sequence in itertools.product(likely_moves, repeat=min(horizon, 2)):
            # Simulate sequence
            sim_position = position.clone()
            
            valid = True
            for move in move_sequence:
                if is_legal(sim_position, move):
                    sim_position.make_move(move)
                else:
                    valid = False
                    break
            
            if not valid:
                continue
            
            # Build exchange graph cho simulated position
            sim_graph = self.build_current_graph(sim_position)
            
            projected.add_scenario(
                moves=move_sequence,
                graph=sim_graph,
                probability=estimate_sequence_probability(
                    position, move_sequence
                )
            )
        
        # Merge scenarios weighted by probability
        projected.merge_scenarios()
        
        return projected
    
    def analyze_trajectory(self):
        """Phân tích xu hướng tiến hóa exchange graph"""
        
        trajectory = EvolutionTrajectory()
        
        all_graphs = [self.current_graph] + [
            pg.merged_graph for pg in self.projected_graphs
        ]
        
        # ═══ CLUSTER EVOLUTION ═══
        
        for cluster_id in self.current_graph.clusters:
            cluster = self.current_graph.clusters[cluster_id]
            
            # Track cluster qua thời gian
            cluster_timeline = [cluster]
            
            for pg in self.projected_graphs:
                matching = pg.find_matching_cluster(cluster)
                cluster_timeline.append(matching)
            
            # Phân tích xu hướng
            intensity_trend = [
                c.intensity if c else 0 for c in cluster_timeline
            ]
            
            if is_increasing(intensity_trend):
                trajectory.intensifying_clusters.append(cluster_id)
            elif is_decreasing(intensity_trend):
                trajectory.dissolving_clusters.append(cluster_id)
            else:
                trajectory.stable_clusters.append(cluster_id)
        
        # ═══ EMERGING CLUSTERS ═══
        # Clusters chưa tồn tại nhưng SẼ xuất hiện
        
        for pg in self.projected_graphs:
            for new_cluster in pg.clusters_not_in(self.current_graph):
                trajectory.emerging_clusters.append(
                    EmergingCluster(
                        cluster=new_cluster,
                        appears_at_horizon=pg.horizon,
                        probability=new_cluster.probability,
                    )
                )
        
        # ═══ TENSION EVOLUTION ═══
        
        tension_values = [
            g.total_tension() if hasattr(g, 'total_tension') else 0 
            for g in all_graphs
        ]
        
        trajectory.tension_trend = compute_trend(tension_values)
        # RISING: tension building → exchanges approaching
        # FALLING: tension releasing → exchanges happening
        # STABLE: equilibrium
        
        return trajectory
    
    def get_emerging_opportunities(self):
        """Lấy các cơ hội trao đổi ĐANG HÌNH THÀNH
        
        Key insight: Cơ hội tốt nhất thường chưa tồn tại ở hiện tại
        nhưng sẽ xuất hiện sau 1-2 nước chuẩn bị
        """
        
        opportunities = []
        
        for emerging in self.evolution_trajectory.emerging_clusters:
            if emerging.probability > 0.3:
                # Tính value nếu exploit cơ hội này
                exploit_value = self.evaluate_emerging_cluster(emerging)
                
                # Tính preparation cost (nước cần để tạo cơ hội)
                prep_cost = estimate_preparation_cost(emerging)
                
                net_value = exploit_value - prep_cost
                
                if net_value > 0:
                    opportunities.append(EmergingOpportunity(
                        cluster=emerging,
                        exploit_value=exploit_value,
                        prep_cost=prep_cost,
                        net_value=net_value,
                        preparation_moves=emerging.enabling_moves,
                    ))
        
        return sorted(opportunities, key=lambda o: -o.net_value)
```

### 3.2 EHP: Exchange Horizon Projector

```python
class ExchangeHorizonProjector:
    """Dự đoán exchange landscape ở "horizons" xa hơn
    
    EGB-E nhìn 1-3 nước → EHP nhìn 5-15 nước
    Sử dụng pattern recognition thay vì brute-force simulation
    
    Key insight: Exchange patterns RHYME
    Similar pawn structures → similar exchange dynamics
    Similar piece placements → similar exchange opportunities
    """
    
    def __init__(self):
        # Pattern database
        self.exchange_patterns = ExchangePatternDatabase()
        
        # Lightweight neural network cho horizon prediction
        self.horizon_net = HorizonPredictionNet()
    
    def project_long_horizon(self, position, ebg_e):
        """Dự đoán exchange landscape ở horizon xa"""
        
        projection = LongHorizonProjection()
        
        # ═══ PATTERN MATCHING ═══
        
        # Tìm patterns tương tự trong database
        current_features = extract_exchange_features(position, ebg_e)
        matching_patterns = self.exchange_patterns.find_similar(
            current_features, top_k=5
        )
        
        for pattern in matching_patterns:
            # Pattern cho biết: "Trong positions tương tự,
            # exchange dynamics phát triển thế nào"
            
            projection.add_pattern_prediction(
                pattern=pattern,
                similarity=pattern.similarity_score,
                predicted_exchanges=pattern.typical_exchange_sequence,
                predicted_timing=pattern.typical_timing,
                predicted_outcome=pattern.typical_outcome,
            )
        
        # ═══ STRUCTURAL PROJECTION ═══
        
        # Dự đoán dựa trên pawn structure evolution
        pawn_trajectory = project_pawn_structure(position)
        
        for future_structure in pawn_trajectory.key_moments:
            # Mỗi pawn structure change → exchange dynamics change
            exchange_implications = analyze_exchange_implications(
                future_structure
            )
            projection.structural_predictions.append(exchange_implications)
        
        # ═══ PIECE LIFECYCLE PROJECTION ═══
        
        for piece in position.all_pieces():
            lifecycle = project_piece_lifecycle(position, piece)
            
            if lifecycle.likely_exchanged:
                projection.piece_fate_predictions[piece] = PieceFate(
                    likely_exchanged=True,
                    exchange_horizon=lifecycle.exchange_horizon,
                    exchange_partner=lifecycle.likely_exchange_partner,
                    post_exchange_value=lifecycle.residual_value,
                )
            elif lifecycle.likely_sacrificed:
                projection.piece_fate_predictions[piece] = PieceFate(
                    likely_sacrificed=True,
                    sacrifice_horizon=lifecycle.sacrifice_horizon,
                    sacrifice_compensation=lifecycle.compensation_estimate,
                )
        
        # ═══ NEURAL PREDICTION ═══
        
        neural_features = extract_neural_features(position, ebg_e)
        neural_prediction = self.horizon_net.predict(neural_features)
        
        projection.neural_prediction = NeuralHorizonPrediction(
            likely_total_exchanges=neural_prediction.exchange_count,
            likely_material_change=neural_prediction.material_delta,
            likely_phase_transition=neural_prediction.phase_change,
            exchange_pace=neural_prediction.exchange_pace,
            # FAST: many exchanges in few moves
            # SLOW: gradual, one-at-a-time
            # BURST: sudden flurry then quiet
        )
        
        return projection
    
    class HorizonPredictionNet:
        """Tiny neural network dự đoán exchange dynamics dài hạn
        
        Input: 64 features (position + exchange graph summary)
        Output: 8 predictions (exchange count, timing, outcome, etc.)
        """
        
        def __init__(self):
            self.layer1 = QuantizedLinear(64, 32, activation='relu')
            self.layer2 = QuantizedLinear(32, 16, activation='relu')
            self.output = QuantizedLinear(16, 8, activation='none')
            # Total: ~3000 parameters, ~3KB, ~2μs inference
        
        def predict(self, features):
            h1 = self.layer1(features)
            h2 = self.layer2(h1)
            raw = self.output(h2)
            
            return HorizonOutput(
                exchange_count=relu(raw[0]) * 10,
                material_delta=raw[1] * 500,
                phase_change=sigmoid(raw[2]),
                exchange_pace=softmax(raw[3:6]),  # [fast, slow, burst]
                tension_evolution=tanh(raw[6]),
                advantage_direction=tanh(raw[7]),
            )
```

### 3.3 TED: Threat Evolution Dynamics

```python
class ThreatEvolutionDynamics:
    """Mô hình hóa SỰ TIẾN HÓA của threats theo thời gian
    
    Giải quyết L5: Binary Threat Model
    
    Key insight: Threats không tĩnh — chúng SINH RA, PHÁT TRIỂN,
    ĐẠT ĐỈNH, và TIÊU TAN. Mô hình lifecycle của threats.
    """
    
    def __init__(self):
        self.threat_registry = {}  # Track known threats
        self.threat_history = []   # History for pattern learning
    
    class ThreatLifecycle:
        """Lifecycle model cho một threat"""
        
        def __init__(self, threat_type, source, target):
            self.type = threat_type
            self.source = source
            self.target = target
            
            # Lifecycle stages
            self.stage = 'LATENT'  # LATENT → DEVELOPING → MATURE → IMMINENT → EXECUTED/DISSOLVED
            
            # Lifecycle metrics
            self.potency = 0.0       # Sức mạnh hiện tại (0-1)
            self.momentum = 0.0      # Đang mạnh lên hay yếu đi
            self.maturation_eta = 0  # Bao nhiêu nước nữa đạt đỉnh
            self.counter_fragility = 0.0  # Dễ bị counter không
            
            # Conditional triggers
            self.conditions = []     # Điều kiện để threat materialize
            self.blockers = []       # Điều kiện ngăn threat
    
    def analyze_threat_evolution(self, position, ebg_e):
        """Phân tích toàn bộ threat evolution landscape"""
        
        evolution = ThreatEvolutionLandscape()
        
        # ═══ IDENTIFY ALL THREATS ═══
        
        threats = self.identify_all_threats(position)
        
        for threat in threats:
            lifecycle = self.model_threat_lifecycle(
                position, threat, ebg_e
            )
            evolution.active_threats.append(lifecycle)
        
        # ═══ TRACK THREAT MOMENTUM ═══
        
        for lifecycle in evolution.active_threats:
            # So sánh với threat state ở nước trước
            previous = self.threat_registry.get(lifecycle.id)
            
            if previous:
                lifecycle.momentum = (
                    lifecycle.potency - previous.potency
                )
            
            # Update registry
            self.threat_registry[lifecycle.id] = lifecycle
        
        # ═══ IDENTIFY CONDITIONAL THREATS ═══
        
        conditional_threats = self.find_conditional_threats(
            position, ebg_e
        )
        
        for cond_threat in conditional_threats:
            evolution.conditional_threats.append(cond_threat)
        
        # ═══ IDENTIFY THREAT CHAINS ═══
        # Threat A enables Threat B enables Threat C...
        
        threat_chains = self.find_threat_chains(evolution.active_threats)
        evolution.threat_chains = threat_chains
        
        # ═══ COMPUTE EXCHANGE THREAT INTERACTION ═══
        # How do exchanges affect threats?
        
        for cluster in ebg_e.current_graph.clusters.values():
            for threat in evolution.active_threats:
                interaction = self.compute_exchange_threat_interaction(
                    cluster, threat
                )
                
                if abs(interaction) > 0.1:
                    evolution.exchange_threat_map.append(
                        ExchangeThreatInteraction(
                            cluster=cluster,
                            threat=threat,
                            interaction=interaction,
                            # positive: exchange strengthens threat
                            # negative: exchange weakens threat
                        )
                    )
        
        return evolution
    
    def model_threat_lifecycle(self, position, threat, ebg_e):
        """Model lifecycle stage and trajectory of a threat"""
        
        lifecycle = self.ThreatLifecycle(
            threat_type=threat.type,
            source=threat.source,
            target=threat.target,
        )
        
        # ═══ DETERMINE STAGE ═══
        
        if threat.is_immediately_executable:
            lifecycle.stage = 'IMMINENT'
            lifecycle.potency = 0.9
            lifecycle.maturation_eta = 0
        
        elif threat.needs_one_preparation_move:
            lifecycle.stage = 'MATURE'
            lifecycle.potency = 0.7
            lifecycle.maturation_eta = 1
        
        elif threat.needs_preparation:
            prep_moves = count_preparation_moves(position, threat)
            
            if prep_moves <= 2:
                lifecycle.stage = 'DEVELOPING'
                lifecycle.potency = 0.4
                lifecycle.maturation_eta = prep_moves
            else:
                lifecycle.stage = 'LATENT'
                lifecycle.potency = 0.2
                lifecycle.maturation_eta = prep_moves
        
        # ═══ ASSESS COUNTER-FRAGILITY ═══
        
        counter_moves = find_counter_moves(position, threat)
        
        if len(counter_moves) == 0:
            lifecycle.counter_fragility = 0.0  # Unstoppable!
        elif len(counter_moves) == 1:
            lifecycle.counter_fragility = 0.3  # Hard to stop
        elif len(counter_moves) <= 3:
            lifecycle.counter_fragility = 0.6
        else:
            lifecycle.counter_fragility = 0.9  # Easy to neutralize
        
        # Adjust potency by counter-fragility
        lifecycle.effective_potency = (
            lifecycle.potency * (1.0 - lifecycle.counter_fragility * 0.5)
        )
        
        # ═══ IDENTIFY CONDITIONS ═══
        
        lifecycle.conditions = identify_threat_conditions(position, threat)
        lifecycle.blockers = identify_threat_blockers(position, threat)
        
        return lifecycle
    
    def find_conditional_threats(self, position, ebg_e):
        """Tìm threats CHỈ tồn tại dưới điều kiện nhất định
        
        Example: Knight fork threat on c7
        BUT ONLY IF opponent castles queenside
        → Conditional on opponent's castling decision
        """
        
        conditional = []
        
        # Check threats that appear after specific opponent moves
        for opp_move in get_likely_opponent_moves(position, n=5):
            sim_pos = simulate_move(position, opp_move)
            
            new_threats = self.identify_all_threats(sim_pos)
            current_threats = self.identify_all_threats(position)
            
            # Threats that appear ONLY after opponent's move
            conditional_threats = [
                t for t in new_threats 
                if not any(t.matches(ct) for ct in current_threats)
            ]
            
            for ct in conditional_threats:
                conditional.append(ConditionalThreat(
                    threat=ct,
                    condition=f"after opponent plays {opp_move}",
                    condition_probability=estimate_move_probability(
                        position, opp_move
                    ),
                    value_if_condition_met=ct.severity,
                ))
        
        # Check threats that appear after specific exchanges
        for cluster in ebg_e.current_graph.clusters.values():
            for sequence in cluster.exchange_sequences:
                post_exchange_pos = apply_exchange(position, sequence)
                
                new_threats = self.identify_all_threats(post_exchange_pos)
                current_threats = self.identify_all_threats(position)
                
                conditional_threats = [
                    t for t in new_threats
                    if not any(t.matches(ct) for ct in current_threats)
                ]
                
                for ct in conditional_threats:
                    conditional.append(ConditionalThreat(
                        threat=ct,
                        condition=f"after exchange {sequence}",
                        condition_probability=0.5,  # Exchange is a choice
                        value_if_condition_met=ct.severity,
                    ))
        
        return conditional
```

---

## IV. Analysis Layer — Phân Tích Hậu Quả

### 4.1 SAE: Synergistic Aftermath Evaluator

```python
class SynergisticAftermathEvaluator:
    """Đánh giá aftermath khi NHIỀU trao đổi xảy ra CÙNG LÚC
    
    Giải quyết L2: Isolated Aftermath Evaluation
    
    Key insight: 
    Aftermath(Exchange_A) + Aftermath(Exchange_B) ≠ Aftermath(A + B)
    
    Synergy > 0: Combined aftermath BETTER than sum of parts
    Synergy < 0: Combined aftermath WORSE (conflicting effects)
    """
    
    def evaluate_combined_aftermath(self, position, exchange_set, 
                                     individual_aftermaths):
        """Evaluate aftermath of combined exchanges"""
        
        result = SynergisticAftermathResult()
        
        # ═══ STEP 1: Apply all exchanges ═══
        
        combined_position = position.clone()
        for exchange in exchange_set:
            combined_position = apply_exchange(combined_position, exchange)
        
        # ═══ STEP 2: Evaluate combined aftermath ═══
        
        combined_aftermath = self.evaluate_full_aftermath(combined_position)
        
        # ═══ STEP 3: Compare with sum of individual aftermaths ═══
        
        sum_individual = sum(
            ia.total_score for ia in individual_aftermaths.values()
        )
        
        synergy = combined_aftermath.total_score - sum_individual
        
        result.combined_score = combined_aftermath.total_score
        result.sum_individual = sum_individual
        result.synergy = synergy
        
        # ═══ STEP 4: Decompose synergy sources ═══
        
        result.synergy_sources = self.decompose_synergy(
            position, combined_position, exchange_set, 
            individual_aftermaths
        )
        
        return result
    
    def decompose_synergy(self, original, combined, exchange_set,
                           individual_aftermaths):
        """Decompose synergy into interpretable sources"""
        
        sources = SynergySources()
        
        # ═══ STRUCTURAL SYNERGY ═══
        # Combined exchanges create pawn structure that neither
        # individual exchange creates
        
        combined_structure = evaluate_pawn_structure(combined)
        
        individual_structures = []
        for exchange in exchange_set:
            single_pos = apply_exchange(original.clone(), exchange)
            individual_structures.append(evaluate_pawn_structure(single_pos))
        
        avg_individual_structure = average_structures(individual_structures)
        sources.structural_synergy = (
            combined_structure.score - avg_individual_structure.score
        )
        
        # ═══ PIECE COORDINATION SYNERGY ═══
        # After combined exchanges, remaining pieces coordinate
        # better/worse than expected
        
        combined_coordination = evaluate_piece_coordination(combined)
        original_coordination = evaluate_piece_coordination(original)
        
        expected_coordination_change = sum(
            ia.coordination_change for ia in individual_aftermaths.values()
        )
        actual_coordination_change = (
            combined_coordination - original_coordination
        )
        
        sources.coordination_synergy = (
            actual_coordination_change - expected_coordination_change
        )
        
        # ═══ KING SAFETY SYNERGY ═══
        # Combined exchanges might expose king more/less than expected
        
        combined_king_safety = evaluate_king_safety_full(combined)
        expected_king_safety_change = sum(
            ia.king_safety_change for ia in individual_aftermaths.values()
        )
        actual_king_safety_change = (
            combined_king_safety - evaluate_king_safety_full(original)
        )
        
        sources.king_safety_synergy = (
            actual_king_safety_change - expected_king_safety_change
        )
        
        # ═══ ACTIVITY SYNERGY ═══
        # Pieces might become more/less active than expected
        
        combined_activity = evaluate_piece_activity(combined)
        expected_activity_change = sum(
            ia.activity_change for ia in individual_aftermaths.values()
        )
        actual_activity_change = (
            combined_activity - evaluate_piece_activity(original)
        )
        
        sources.activity_synergy = (
            actual_activity_change - expected_activity_change
        )
        
        # ═══ IMBALANCE SYNERGY ═══
        # Combined exchanges might create favorable/unfavorable imbalance
        
        combined_imbalance = evaluate_material_imbalance(combined)
        sources.imbalance_synergy = combined_imbalance.synergy_component
        
        return sources
    
    def find_synergistic_exchange_sets(self, position, ebg_e, top_k=5):
        """Tìm top-K tập exchange sets có synergy cao nhất
        
        Thay vì evaluate ALL combinations (exponential),
        sử dụng heuristics để tìm candidates likely synergistic
        """
        
        clusters = list(ebg_e.current_graph.clusters.values())
        
        if len(clusters) < 2:
            return []  # Need at least 2 clusters for synergy
        
        candidates = []
        
        # Heuristic 1: Clusters on same file/rank
        for i, c1 in enumerate(clusters):
            for c2 in clusters[i+1:]:
                if on_same_file(c1.square, c2.square):
                    candidates.append((c1, c2, 'file_alignment'))
                elif on_same_rank(c1.square, c2.square):
                    candidates.append((c1, c2, 'rank_alignment'))
                elif on_same_diagonal(c1.square, c2.square):
                    candidates.append((c1, c2, 'diagonal_alignment'))
        
        # Heuristic 2: Clusters involving same piece type
        for i, c1 in enumerate(clusters):
            for c2 in clusters[i+1:]:
                shared_types = (
                    set(p.type for p in c1.pieces) & 
                    set(p.type for p in c2.pieces)
                )
                if shared_types:
                    candidates.append((c1, c2, 'shared_piece_type'))
        
        # Heuristic 3: Clusters where one enables the other
        for c1, c2, _ in list(candidates):
            if c1.involves_defender_of(c2) or c2.involves_defender_of(c1):
                candidates.append((c1, c2, 'defender_removal'))
        
        # Evaluate candidates
        results = []
        for c1, c2, reason in candidates[:20]:  # Limit to top 20
            best_seq1 = c1.best_exchange_sequence
            best_seq2 = c2.best_exchange_sequence
            
            if best_seq1 and best_seq2:
                individual_1 = self.evaluate_single_aftermath(
                    position, best_seq1
                )
                individual_2 = self.evaluate_single_aftermath(
                    position, best_seq2
                )
                
                combined = self.evaluate_combined_aftermath(
                    position,
                    [best_seq1, best_seq2],
                    {c1.id: individual_1, c2.id: individual_2}
                )
                
                results.append(SynergisticSet(
                    clusters=(c1, c2),
                    reason=reason,
                    synergy=combined.synergy,
                    combined_score=combined.combined_score,
                ))
        
        results.sort(key=lambda r: -r.synergy)
        return results[:top_k]
```

### 4.2 HOIT: Higher-Order Interaction Tensor

```python
class HigherOrderInteractionTensor:
    """Mở rộng CCIM từ pairwise thành higher-order interactions
    
    Giải quyết L4: Pairwise Cluster Interaction
    
    CCIM gốc: Matrix M[i][j] = interaction(cluster_i, cluster_j)
    HOIT: Tensor T[i][j][k] = interaction(cluster_i, cluster_j, cluster_k)
    
    Key insight: 3-way interactions xuất hiện thường xuyên trong chess:
    - Sacrifice on h7 + Exchange on d5 + Open e-file = coordinated attack
    - These 3 interact synergistically but NO pair alone explains it
    """
    
    def compute(self, position, ebg_e, max_order=3):
        """Compute interaction tensor up to given order"""
        
        clusters = list(ebg_e.current_graph.clusters.values())
        n = len(clusters)
        
        result = InteractionTensor(n_clusters=n, max_order=max_order)
        
        # ═══ ORDER 2: Pairwise (CCIM enhanced) ═══
        
        for i in range(n):
            for j in range(i + 1, n):
                interaction = self.compute_pairwise(
                    position, clusters[i], clusters[j]
                )
                result.set_pairwise(i, j, interaction)
        
        # ═══ ORDER 3: Three-way (NEW in HOIT) ═══
        
        if n >= 3 and max_order >= 3:
            # Only compute for promising triples
            promising_triples = self.find_promising_triples(
                clusters, result
            )
            
            for i, j, k in promising_triples:
                interaction = self.compute_triple(
                    position, clusters[i], clusters[j], clusters[k]
                )
                result.set_triple(i, j, k, interaction)
        
        return result
    
    def find_promising_triples(self, clusters, pairwise_results):
        """Find triples likely to have significant 3-way interaction
        
        Heuristic: If all 3 pairs have significant pairwise interaction,
        the triple likely has significant 3-way interaction too
        """
        
        n = len(clusters)
        promising = []
        
        for i in range(n):
            for j in range(i + 1, n):
                for k in range(j + 1, n):
                    # Check if all pairwise interactions are significant
                    ij = abs(pairwise_results.get_pairwise(i, j))
                    jk = abs(pairwise_results.get_pairwise(j, k))
                    ik = abs(pairwise_results.get_pairwise(i, k))
                    
                    min_pair = min(ij, jk, ik)
                    
                    if min_pair > 0.2:  # All pairs have some interaction
                        promising.append((i, j, k, min_pair))
        
        # Sort by strength and limit
        promising.sort(key=lambda x: -x[3])
        return [(i, j, k) for i, j, k, _ in promising[:10]]
    
    def compute_triple(self, position, c1, c2, c3):
        """Compute 3-way interaction
        
        3-way interaction = combined effect - sum of pairwise effects
        
        If positive: 3 clusters together create MORE than sum of pairs
        If negative: 3 clusters together create LESS (interference)
        """
        
        # Get best sequences
        seq1, seq2, seq3 = (
            c1.best_exchange_sequence,
            c2.best_exchange_sequence,
            c3.best_exchange_sequence,
        )
        
        if not all([seq1, seq2, seq3]):
            return 0.0
        
        # Evaluate all combinations
        eval_1 = evaluate_after_exchange(position, [seq1])
        eval_2 = evaluate_after_exchange(position, [seq2])
        eval_3 = evaluate_after_exchange(position, [seq3])
        eval_12 = evaluate_after_exchange(position, [seq1, seq2])
        eval_13 = evaluate_after_exchange(position, [seq1, seq3])
        eval_23 = evaluate_after_exchange(position, [seq2, seq3])
        eval_123 = evaluate_after_exchange(position, [seq1, seq2, seq3])
        eval_0 = evaluate_position(position)
        
        # 3-way interaction (Möbius-like formula)
        # I(1,2,3) = E(1,2,3) - E(1,2) - E(1,3) - E(2,3) + E(1) + E(2) + E(3) - E(0)
        triple_interaction = (
            eval_123 - eval_12 - eval_13 - eval_23 
            + eval_1 + eval_2 + eval_3 - eval_0
        )
        
        return triple_interaction / 100.0  # Normalize
```

### 4.3 MTP: Material Transform Projector

```python
class MaterialTransformProjector:
    """Dự đoán cách exchanges biến đổi material composition
    và ảnh hưởng đến endgame character
    
    Giải quyết L8: Material Transformation Awareness
    
    Key insight: Giá trị trao đổi phụ thuộc vào CÁI GÌ CÒN LẠI
    R+B vs R+N khác biệt tùy pawn structure SAU trao đổi
    """
    
    def project_material_transform(self, position, exchange_scenarios):
        """Project material composition after various exchange scenarios"""
        
        projections = []
        
        for scenario in exchange_scenarios:
            # Apply exchanges
            result_position = apply_all_exchanges(position, scenario)
            
            # Analyze resulting material
            material = MaterialComposition(result_position)
            
            # ═══ IMBALANCE ANALYSIS ═══
            
            imbalance = self.analyze_imbalance(
                material, result_position
            )
            
            # ═══ ENDGAME PROJECTION ═══
            
            endgame_type = self.classify_endgame(material)
            endgame_advantage = self.evaluate_endgame_advantage(
                result_position, endgame_type, material
            )
            
            # ═══ PAWN STRUCTURE FIT ═══
            # How well does remaining material fit pawn structure?
            
            structure_fit = self.evaluate_structure_fit(
                result_position, material
            )
            
            projections.append(MaterialProjection(
                scenario=scenario,
                resulting_material=material,
                imbalance=imbalance,
                endgame_type=endgame_type,
                endgame_advantage=endgame_advantage,
                structure_fit=structure_fit,
                total_projection_score=(
                    imbalance.score * 0.3 +
                    endgame_advantage * 0.4 +
                    structure_fit * 0.3
                ),
            ))
        
        return projections
    
    def analyze_imbalance(self, material, position):
        """Analyze material imbalance details"""
        
        imbalance = MaterialImbalance()
        
        # ═══ BISHOP PAIR ═══
        
        if material.white_has_bishop_pair:
            openness = compute_openness(position)
            imbalance.bishop_pair_advantage = 50 + int(openness * 100)
            imbalance.favors = WHITE
        elif material.black_has_bishop_pair:
            openness = compute_openness(position)
            imbalance.bishop_pair_advantage = -(50 + int(openness * 100))
            imbalance.favors = BLACK
        
        # ═══ ROOK vs MINOR PIECES ═══
        
        rook_advantage_white = (
            material.white_rooks - material.black_rooks
        )
        minor_advantage_black = (
            (material.black_knights + material.black_bishops) -
            (material.white_knights + material.white_bishops)
        )
        
        if rook_advantage_white > 0 and minor_advantage_black > 0:
            # White has exchange advantage
            # Value depends on open files, passed pawns
            exchange_value = self.evaluate_exchange_imbalance(
                position, WHITE
            )
            imbalance.exchange_imbalance = exchange_value
        
        # ═══ KNIGHT vs BISHOP ═══
        
        if (material.white_knights > material.white_bishops and
            material.black_bishops > material.black_knights):
            # White has knights, Black has bishops
            # Depends on pawn structure
            
            closed_ness = 1.0 - compute_openness(position)
            
            if closed_ness > 0.6:
                imbalance.knight_bishop_imbalance = 30  # Knights better
            else:
                imbalance.knight_bishop_imbalance = -30  # Bishops better
        
        # ═══ OVERALL IMBALANCE SCORE ═══
        
        imbalance.score = (
            imbalance.bishop_pair_advantage +
            imbalance.exchange_imbalance +
            imbalance.knight_bishop_imbalance
        ) / 100.0
        
        return imbalance
    
    def evaluate_structure_fit(self, position, material):
        """How well does remaining material fit the pawn structure?"""
        
        fit = 0.0
        
        # ═══ BISHOP COLOR FIT ═══
        
        for side in [WHITE, BLACK]:
            sign = 1 if side == WHITE else -1
            
            if material.has_single_bishop(side):
                bishop_color = material.bishop_color(side)
                
                # Count pawns on bishop's color
                pawns_on_color = count_pawns_on_color(
                    position, side, bishop_color
                )
                total_pawns = count_pawns(position, side)
                
                # Fewer pawns on bishop's color = better bishop
                if total_pawns > 0:
                    pawn_ratio = pawns_on_color / total_pawns
                    
                    if pawn_ratio < 0.3:
                        fit += sign * 0.3  # Good bishop
                    elif pawn_ratio > 0.6:
                        fit -= sign * 0.2  # Bad bishop
        
        # ═══ ROOK + OPEN FILE FIT ═══
        
        for side in [WHITE, BLACK]:
            sign = 1 if side == WHITE else -1
            
            if material.rook_count(side) > 0:
                open_files = count_open_semi_open_files(position, side)
                
                fit += sign * min(open_files * 0.1, 0.3)
        
        # ═══ KNIGHT + OUTPOST FIT ═══
        
        for side in [WHITE, BLACK]:
            sign = 1 if side == WHITE else -1
            
            if material.knight_count(side) > 0:
                outposts = count_outpost_squares(position, side)
                
                fit += sign * min(outposts * 0.08, 0.25)
        
        # ═══ PASSED PAWN + PIECE FIT ═══
        
        for side in [WHITE, BLACK]:
            sign = 1 if side == WHITE else -1
            
            passed = count_passed_pawns(position, side)
            
            if passed > 0:
                # Rook behind passed pawn = great fit
                if material.rook_count(side) > 0:
                    fit += sign * 0.2
                
                # King near passed pawn in endgame = great fit
                if material.total_non_pawn(1 - side) <= ROOK_VALUE:
                    king_dist = king_distance_to_pawn(
                        position, side
                    )
                    fit += sign * max(0, (7 - king_dist) * 0.03)
        
        return fit
```

---

## V. Strategy Layer — Khi Nào và Tại Sao Trao Đổi

### 5.1 CTE: Conditional Temporal Engine

```python
class ConditionalTemporalEngine:
    """Engine quyết định THỜI ĐIỂM trao đổi dựa trên điều kiện
    
    Giải quyết L3: Linear Temporal Model
    
    Thay vì: "exchange value decays exponentially with time"
    CTE: "exchange when CONDITIONS are met, not just when time passes"
    """
    
    def evaluate_timing(self, position, exchange, ebg_e, ted):
        """Đánh giá timing tối ưu cho một exchange"""
        
        timing = TimingEvaluation()
        
        # ═══ TRIGGER-BASED TIMING ═══
        
        triggers = self.identify_exchange_triggers(position, exchange)
        
        for trigger in triggers:
            timing.triggers.append(TimingTrigger(
                condition=trigger.condition,
                probability=trigger.probability,
                value_if_triggered=trigger.value_multiplier,
                expected_timing=trigger.eta,
            ))
        
        # ═══ WINDOW OF OPPORTUNITY ═══
        
        window = self.compute_opportunity_window(
            position, exchange, ebg_e
        )
        
        timing.window = window
        # window.opens_at: nước nào cửa sổ mở
        # window.closes_at: nước nào cửa sổ đóng
        # window.peak_at: nước nào giá trị cao nhất
        # window.is_now_open: đang trong cửa sổ không
        
        # ═══ OPPONENT-DEPENDENT TIMING ═══
        
        opp_timing = self.evaluate_opponent_timing_preference(
            position, exchange
        )
        
        timing.opponent_wants_exchange = opp_timing.wants_exchange
        timing.opponent_timing = opp_timing.preferred_timing
        
        # If opponent also wants exchange → less valuable to delay
        # If opponent doesn't want exchange → more valuable to threaten
        
        if opp_timing.wants_exchange:
            timing.delay_value = -50  # Don't delay, opponent benefits too
        else:
            timing.delay_value = 50   # Delay is powerful (threat > execution)
        
        # ═══ PHASE-TRANSITION TIMING ═══
        
        phase_timing = self.evaluate_phase_timing(position, exchange)
        timing.phase_score = phase_timing
        
        # ═══ CONDITIONAL VALUE ═══
        # "Exchange value IF condition X is true"
        
        for trigger in timing.triggers:
            if trigger.probability > 0.5:
                timing.conditional_values.append(ConditionalValue(
                    condition=trigger.condition,
                    value=exchange.base_value * trigger.value_multiplier,
                    probability=trigger.probability,
                    expected_value=exchange.base_value * trigger.value_multiplier * trigger.probability,
                ))
        
        # ═══ OPTIMAL TIMING RECOMMENDATION ═══
        
        timing.optimal_timing = self.compute_optimal_timing(timing)
        
        return timing
    
    def identify_exchange_triggers(self, position, exchange):
        """Identify conditions that make exchange especially valuable"""
        
        triggers = []
        
        # Trigger 1: Opponent commits to specific plan
        for opponent_plan in identify_likely_opponent_plans(position):
            value_after_plan = evaluate_exchange_after_plan(
                position, exchange, opponent_plan
            )
            value_before_plan = evaluate_exchange_value(position, exchange)
            
            if value_after_plan > value_before_plan + 50:
                triggers.append(Trigger(
                    condition=f"after opponent commits to {opponent_plan.name}",
                    probability=opponent_plan.probability,
                    value_multiplier=value_after_plan / max(value_before_plan, 1),
                    eta=opponent_plan.commitment_moves,
                ))
        
        # Trigger 2: Specific pawn move creates opportunity
        for pawn_move in get_relevant_pawn_moves(position):
            sim = simulate_move(position, pawn_move)
            value_after = evaluate_exchange_value(sim, exchange)
            value_before = evaluate_exchange_value(position, exchange)
            
            if value_after > value_before + 30:
                triggers.append(Trigger(
                    condition=f"after pawn move {pawn_move}",
                    probability=0.4,
                    value_multiplier=value_after / max(value_before, 1),
                    eta=1,
                ))
        
        # Trigger 3: Piece reaches specific square
        for piece in exchange.involved_pieces:
            ideal_square = find_ideal_exchange_square(position, piece, exchange)
            
            if ideal_square != piece.square:
                moves_to_ideal = distance_in_moves(
                    piece, ideal_square, position
                )
                
                if moves_to_ideal <= 3:
                    triggers.append(Trigger(
                        condition=f"after {piece} reaches {ideal_square}",
                        probability=0.6,
                        value_multiplier=1.3,
                        eta=moves_to_ideal,
                    ))
        
        return triggers
    
    def compute_opportunity_window(self, position, exchange, ebg_e):
        """Compute the window of opportunity for an exchange"""
        
        window = OpportunityWindow()
        
        # Check if exchange is currently possible
        window.is_now_open = exchange.is_currently_executable
        
        # Project forward: when does opportunity appear/disappear?
        for horizon in range(1, 8):
            projected = ebg_e.projected_graphs[min(
                horizon - 1, len(ebg_e.projected_graphs) - 1
            )]
            
            can_exchange_at_horizon = projected.has_exchange(exchange)
            
            if not window.is_now_open and can_exchange_at_horizon:
                window.opens_at = horizon
            
            if window.is_now_open and not can_exchange_at_horizon:
                window.closes_at = horizon
        
        # Estimate peak value timing
        if window.opens_at is not None and window.closes_at is not None:
            window.peak_at = (window.opens_at + window.closes_at) // 2
        elif window.is_now_open:
            window.peak_at = 0  # Now might be peak
        
        # Window urgency
        if window.closes_at is not None and window.closes_at <= 2:
            window.urgency = 1.0  # Closing soon!
        elif window.closes_at is not None and window.closes_at <= 5:
            window.urgency = 0.6
        else:
            window.urgency = 0.2
        
        return window
```

### 5.2 ENM: Exchange Negotiation Model

```python
class ExchangeNegotiationModel:
    """Model exchange dynamics như một "negotiation"
    
    Giải quyết L6: No Opponent Modeling
    
    Key insight: Exchange là TWO-SIDED decision
    Cả hai bên phải ĐỒNG Ý (hoặc bị ÉP) trao đổi
    → Model as negotiation/game theory
    """
    
    def evaluate_negotiation(self, position, exchange):
        """Evaluate exchange from negotiation perspective"""
        
        negotiation = NegotiationEvaluation()
        
        # ═══ WHO INITIATES? ═══
        
        initiator = exchange.initiating_side
        responder = 1 - initiator
        
        # ═══ INITIATOR'S PERSPECTIVE ═══
        
        initiator_value = self.evaluate_for_side(
            position, exchange, initiator
        )
        negotiation.initiator_value = initiator_value
        
        # ═══ RESPONDER'S PERSPECTIVE ═══
        
        responder_value = self.evaluate_for_side(
            position, exchange, responder
        )
        negotiation.responder_value = responder_value
        
        # ═══ NEGOTIATION DYNAMICS ═══
        
        # Both want exchange? → Exchange likely, value = average
        # Only one wants? → Has leverage
        # Neither wants? → Exchange unlikely, threat value matters
        
        initiator_wants = initiator_value > 0
        responder_wants = responder_value > 0
        
        if initiator_wants and responder_wants:
            negotiation.dynamics = 'MUTUAL_DESIRE'
            negotiation.leverage = 0.0  # Neither has leverage
            negotiation.exchange_likelihood = 0.9
            
            # In mutual desire: whoever benefits MORE should initiate
            if initiator_value > responder_value:
                negotiation.initiator_advantage = True
            else:
                negotiation.initiator_advantage = False
        
        elif initiator_wants and not responder_wants:
            negotiation.dynamics = 'FORCED_EXCHANGE'
            negotiation.leverage = initiator_value  # Initiator has leverage
            negotiation.exchange_likelihood = 0.6
            
            # Responder might avoid exchange → cost of avoidance
            avoidance_cost = self.compute_avoidance_cost(
                position, exchange, responder
            )
            negotiation.avoidance_cost = avoidance_cost
            
            if avoidance_cost > abs(responder_value):
                # Too expensive to avoid → exchange likely
                negotiation.exchange_likelihood = 0.8
        
        elif not initiator_wants and responder_wants:
            negotiation.dynamics = 'RESPONDER_DESIRE'
            negotiation.leverage = -responder_value  # Responder wants it
            negotiation.exchange_likelihood = 0.4
            
            # Initiator should probably AVOID exchange
            negotiation.recommended_action = 'AVOID'
        
        else:  # Neither wants
            negotiation.dynamics = 'MUTUAL_AVOIDANCE'
            negotiation.leverage = 0.0
            negotiation.exchange_likelihood = 0.1
            
            # Both avoiding: tension remains
            # Threat value might exceed exchange value
            negotiation.recommended_action = 'MAINTAIN_TENSION'
        
        # ═══ EXCHANGE POWER BALANCE ═══
        
        negotiation.power_balance = self.compute_power_balance(
            position, exchange
        )
        
        # ═══ ALTERNATIVE ANALYSIS ═══
        # What if exchange DOESN'T happen?
        
        negotiation.non_exchange_value = self.evaluate_non_exchange(
            position, exchange
        )
        
        return negotiation
    
    def compute_avoidance_cost(self, position, exchange, avoiding_side):
        """Cost for 'avoiding_side' to prevent the exchange"""
        
        cost = 0
        
        # Cost 1: Piece must retreat → tempo loss
        target_piece = exchange.target_piece(avoiding_side)
        
        safe_retreats = count_safe_retreats(position, target_piece)
        if safe_retreats == 0:
            cost += 500  # No retreat possible! Must accept exchange
        elif safe_retreats == 1:
            cost += 200  # Very limited options
        elif safe_retreats <= 3:
            cost += 100
        else:
            cost += 30
        
        # Cost 2: Retreating piece becomes passive
        best_retreat = find_best_retreat(position, target_piece)
        if best_retreat:
            activity_loss = (
                piece_activity_score(target_piece, target_piece.square) -
                piece_activity_score(target_piece, best_retreat)
            )
            cost += max(0, activity_loss)
        
        # Cost 3: Other pieces must rearrange
        defenders_needed = count_extra_defenders_needed(
            position, target_piece.square, avoiding_side
        )
        cost += defenders_needed * 50
        
        # Cost 4: Strategic concessions
        if avoiding_exchange_concedes_outpost(position, exchange, avoiding_side):
            cost += 80
        
        if avoiding_exchange_concedes_open_file(position, exchange, avoiding_side):
            cost += 60
        
        return cost
    
    def evaluate_non_exchange(self, position, exchange):
        """Value of maintaining the current tension (not exchanging)"""
        
        value = 0
        
        # Value of threatening exchange (TAQ concept, enhanced)
        threat_value = compute_enhanced_threat_value(position, exchange)
        value += threat_value
        
        # Value of flexibility (keeping exchange as option)
        flexibility_value = compute_flexibility_value(position, exchange)
        value += flexibility_value
        
        # Value of maintaining pressure
        pressure_value = compute_pressure_value(position, exchange)
        value += pressure_value
        
        return value
```

### 5.3 SEA: Strategic Exchange Alignment

```python
class StrategicExchangeAlignment:
    """Align exchange decisions with strategic plan
    
    Giải quyết L7: No Plan Integration
    
    Key insight: "Good exchange" depends on PLAN
    Same exchange might be brilliant for one plan, terrible for another
    """
    
    def evaluate_alignment(self, position, exchange, strategic_context):
        """Evaluate how well exchange aligns with current strategic plan"""
        
        alignment = AlignmentEvaluation()
        
        # ═══ IDENTIFY ACTIVE PLANS ═══
        
        plans = strategic_context.active_plans
        
        if not plans:
            # No clear plan → exchange evaluation is plan-neutral
            alignment.total_alignment = 0.0
            return alignment
        
        # ═══ EVALUATE ALIGNMENT PER PLAN ═══
        
        for plan in plans:
            plan_alignment = self.evaluate_plan_alignment(
                position, exchange, plan
            )
            
            alignment.plan_alignments[plan.name] = plan_alignment
        
        # ═══ AGGREGATE ALIGNMENT ═══
        # Weighted by plan confidence/priority
        
        total_alignment = 0.0
        total_weight = 0.0
        
        for plan in plans:
            pa = alignment.plan_alignments[plan.name]
            weight = plan.confidence * plan.priority
            
            total_alignment += pa.score * weight
            total_weight += weight
        
        if total_weight > 0:
            alignment.total_alignment = total_alignment / total_weight
        
        return alignment
    
    def evaluate_plan_alignment(self, position, exchange, plan):
        """Evaluate alignment of specific exchange with specific plan"""
        
        result = PlanAlignmentResult()
        
        # ═══ PIECE ROLE IN PLAN ═══
        
        for piece in exchange.pieces_being_exchanged:
            role = plan.get_piece_role(piece)
            
            if role == 'KEY_ATTACKER':
                # Exchanging key attacking piece = BAD for attack plan
                result.score -= 0.5
                result.reasons.append(
                    f"Losing key attacker {piece} for {plan.name}"
                )
            
            elif role == 'KEY_DEFENDER':
                # Exchanging key defender = GOOD for attack (if our plan)
                # BAD if it's our defender
                if piece.side == position.side_to_move:
                    result.score -= 0.4
                else:
                    result.score += 0.4
                    result.reasons.append(
                        f"Removing opponent's key defender {piece}"
                    )
            
            elif role == 'BLOCKING':
                # Exchanging blocking piece = GOOD (clears path)
                result.score += 0.3
                result.reasons.append(
                    f"Removing blocking piece {piece}"
                )
            
            elif role == 'SUPPORTING':
                if piece.side == position.side_to_move:
                    result.score -= 0.2  # Losing support
                else:
                    result.score += 0.2  # Removing opponent support
        
        # ═══ EXCHANGE EFFECT ON PLAN VIABILITY ═══
        
        result_position = apply_exchange(position, exchange.sequence)
        
        plan_viability_before = plan.evaluate_viability(position)
        plan_viability_after = plan.evaluate_viability(result_position)
        
        viability_change = plan_viability_after - plan_viability_before
        
        result.score += viability_change * 0.5
        
        if viability_change > 0.3:
            result.reasons.append(
                f"Exchange enhances {plan.name} viability by {viability_change:.1%}"
            )
        elif viability_change < -0.3:
            result.reasons.append(
                f"Exchange weakens {plan.name} viability by {abs(viability_change):.1%}"
            )
        
        # ═══ STRUCTURAL ALIGNMENT ═══
        
        plan_structure = plan.desired_pawn_structure
        
        if plan_structure:
            exchange_structure = evaluate_pawn_structure(result_position)
            
            structure_alignment = compare_structures(
                exchange_structure, plan_structure
            )
            
            result.score += structure_alignment * 0.3
        
        # ═══ TIMING ALIGNMENT ═══
        
        if plan.timing_phase:
            exchange_timing = evaluate_exchange_timing(position, exchange)
            
            if exchange_timing.phase == plan.timing_phase:
                result.score += 0.2  # Good timing alignment
            else:
                result.score -= 0.1  # Timing mismatch
        
        return result
```

---

## VI. Integration Layer

### 6.1 Graduated Computation Controller

```python
class GraduatedComputationController:
    """Control DEE-X computation cost
    
    Giải quyết L9: Computational Bottleneck
    
    DEE-X has MANY components → can be very expensive
    → Must GRADUATE computation based on importance
    """
    
    TIERS = {
        'INSTANT': {
            # Cost: <1μs
            # When: EVERY node
            'components': ['basic_exchange_count', 'obvious_tactics'],
            'description': 'Minimal check: is DEE-X even relevant?'
        },
        'QUICK': {
            # Cost: 2-5μs
            # When: Nodes where exchanges exist
            'components': ['EGB_current', 'basic_PAE', 'basic_TAQ'],
            'description': 'Quick DEE evaluation (like DEE gốc lite)'
        },
        'STANDARD': {
            # Cost: 10-30μs
            # When: Important nodes (near root, PV, critical)
            'components': [
                'EGB-E_1step', 'PAE-S', 'TDM-C_basic', 
                'CCIM_pairwise', 'TAQ-E_basic'
            ],
            'description': 'Full DEE gốc + basic extensions'
        },
        'DEEP': {
            # Cost: 50-200μs
            # When: Root nodes, critical decisions
            'components': [
                'EGB-E_full', 'EHP', 'TED', 'SAE', 'HOIT',
                'CTE', 'ENM', 'SEA', 'MTP'
            ],
            'description': 'Full DEE-X analysis'
        },
        'EXHAUSTIVE': {
            # Cost: 500μs-2ms
            # When: Root only, complex exchange decisions
            'components': [
                'ALL', 'multi_scenario', 'neural_verification'
            ],
            'description': 'Maximum analysis depth'
        },
    }
    
    def select_tier(self, position, search_state):
        """Select appropriate computation tier"""
        
        # Root node: always DEEP or EXHAUSTIVE
        if search_state.depth_from_root == 0:
            if has_complex_exchange_landscape(position):
                return 'EXHAUSTIVE'
            return 'DEEP'
        
        # Near root (1-3 ply): STANDARD or DEEP
        if search_state.depth_from_root <= 3:
            if search_state.is_pv_node:
                return 'DEEP'
            return 'STANDARD'
        
        # Mid-tree (4-8 ply): QUICK or STANDARD
        if search_state.depth_from_root <= 8:
            if has_active_exchanges(position):
                return 'STANDARD'
            return 'QUICK'
        
        # Deep tree (9+ ply): INSTANT or QUICK
        if has_any_exchange_possibility(position):
            return 'QUICK'
        
        return 'INSTANT'
    
    def execute_tier(self, tier, position, search_state, dee_x):
        """Execute DEE-X at specified tier"""
        
        config = self.TIERS[tier]
        components = config['components']
        
        result = DEEXResult(tier=tier)
        
        if tier == 'INSTANT':
            # Just check if exchanges are relevant
            result.has_exchanges = has_any_capture(position)
            result.exchange_relevance = 0.0 if not result.has_exchanges else 0.5
            return result
        
        if tier == 'QUICK':
            # Basic DEE evaluation
            result.basic_graph = dee_x.ebg_e.build_current_only(position)
            result.basic_pae = dee_x.pae_s.evaluate_basic(position, result.basic_graph)
            result.basic_taq = dee_x.taq_e.evaluate_basic(position, result.basic_graph)
            result.score = result.basic_pae.score + result.basic_taq.score
            return result
        
        if tier == 'STANDARD':
            # DEE gốc + basic extensions
            result.graph = dee_x.ebg_e.build(position, depth=1)
            result.pae = dee_x.pae_s.evaluate(position, result.graph)
            result.taq = dee_x.taq_e.evaluate(position, result.graph)
            result.ccim = dee_x.hoit.compute(position, result.graph, max_order=2)
            result.timing = dee_x.cte.evaluate_basic_timing(position, result.graph)
            
            result.score = dee_x.aggregate_standard(result)
            return result
        
        if tier in ['DEEP', 'EXHAUSTIVE']:
            # Full DEE-X
            result.graph = dee_x.ebg_e.build(position, depth=3)
            result.horizon = dee_x.ehp.project_long_horizon(position, result.graph)
            result.threats = dee_x.ted.analyze_threat_evolution(position, result.graph)
            result.pae = dee_x.pae_s.evaluate_full(position, result.graph)
            result.synergy = dee_x.sae.find_synergistic_exchange_sets(position, result.graph)
            result.hoit = dee_x.hoit.compute(position, result.graph, max_order=3)
            result.material = dee_x.mtp.project_material_transform(position, result.graph.all_scenarios)
            result.timing = dee_x.cte.evaluate_timing_all(position, result.graph, result.threats)
            result.negotiation = dee_x.enm.evaluate_negotiation_all(position, result.graph)
            result.alignment = dee_x.sea.evaluate_alignment_all(position, result.graph, search_state)
            
            result.score = dee_x.aggregate_full(result)
            
            if tier == 'EXHAUSTIVE':
                # Additional neural verification
                result.neural_verification = dee_x.dle.verify(result)
            
            return result
```

### 6.2 DEE-X Learning Engine

```python
class DEEXLearningEngine:
    """Self-improvement engine for DEE-X
    
    Giải quyết L10: No Learning From Games
    """
    
    def __init__(self):
        # Weight learning
        self.component_weights = {
            'pae': 1.0, 'taq': 1.0, 'ccim': 0.8,
            'timing': 0.7, 'negotiation': 0.6,
            'alignment': 0.5, 'material': 0.7,
            'synergy': 0.6, 'threats': 0.7,
            'horizon': 0.4,
        }
        
        # Accuracy tracking per component
        self.component_accuracy = defaultdict(
            lambda: RunningAccuracy(window=1000)
        )
        
        # Pattern learning
        self.pattern_database = ExchangePatternDatabase()
        
        # Learning rate
        self.lr = 0.005
    
    def on_position_resolved(self, dee_x_result, actual_outcome):
        """Learn from resolved position"""
        
        # Compare each component's prediction with actual outcome
        for component_name, prediction in dee_x_result.component_predictions.items():
            error = abs(prediction - actual_outcome)
            
            self.component_accuracy[component_name].record(
                was_accurate=(error < 50)
            )
        
        # Adjust weights based on accuracy
        self.adjust_weights()
        
        # Learn exchange patterns
        if dee_x_result.tier in ['DEEP', 'EXHAUSTIVE']:
            self.learn_pattern(dee_x_result, actual_outcome)
    
    def adjust_weights(self):
        """Adjust component weights based on accuracy"""
        
        for name in self.component_weights:
            accuracy = self.component_accuracy[name].value
            
            if accuracy is None:
                continue
            
            if accuracy > 0.65:
                # Accurate component → increase weight
                self.component_weights[name] *= (1 + self.lr)
            elif accuracy < 0.45:
                # Inaccurate → decrease weight
                self.component_weights[name] *= (1 - self.lr)
            
            # Clamp
            self.component_weights[name] = clamp(
                self.component_weights[name], 0.1, 3.0
            )
    
    def learn_pattern(self, dee_x_result, actual_outcome):
        """Learn exchange pattern from resolved position"""
        
        features = extract_exchange_features_from_result(dee_x_result)
        
        pattern = ExchangePattern(
            features=features,
            predicted_outcome=dee_x_result.score,
            actual_outcome=actual_outcome,
            exchange_sequence=dee_x_result.best_exchange_sequence,
            position_type=dee_x_result.position_type,
        )
        
        self.pattern_database.add(pattern)
```

### 6.3 Score Aggregation

```python
class DEEXScoreAggregator:
    """Aggregate all DEE-X component scores into final score"""
    
    def aggregate_full(self, result, dle):
        """Full aggregation with all components"""
        
        weights = dle.component_weights
        
        components = {}
        
        # ═══ CORE COMPONENTS ═══
        
        if result.pae:
            components['pae'] = result.pae.total_score
        
        if result.taq:
            components['taq'] = result.taq.net_threat_value
        
        if result.hoit:
            components['ccim'] = result.hoit.total_interaction_value
        
        # ═══ EXTENDED COMPONENTS ═══
        
        if result.timing:
            components['timing'] = result.timing.optimal_timing_value
        
        if result.negotiation:
            components['negotiation'] = result.negotiation.net_negotiation_value
        
        if result.alignment:
            components['alignment'] = result.alignment.total_alignment * 100
        
        if result.material:
            best_projection = max(
                result.material, key=lambda p: p.total_projection_score
            )
            components['material'] = best_projection.total_projection_score * 100
        
        if result.synergy:
            if result.synergy:
                best_synergy = result.synergy[0]
                components['synergy'] = best_synergy.synergy * 100
        
        if result.threats:
            components['threats'] = result.threats.net_threat_value
        
        if result.horizon:
            components['horizon'] = result.horizon.advantage_direction * 50
        
        # ═══ WEIGHTED SUM ═══
        
        total_score = 0.0
        total_weight = 0.0
        
        for name, value in components.items():
            w = weights.get(name, 0.5)
            total_score += value * w
            total_weight += w
        
        if total_weight > 0:
            final_score = total_score / total_weight
        else:
            final_score = 0.0
        
        # ═══ NONLINEAR INTERACTIONS ═══
        
        # Synergy bonus when multiple components agree
        positive_count = sum(1 for v in components.values() if v > 20)
        negative_count = sum(1 for v in components.values() if v < -20)
        
        if positive_count >= 4:
            final_score *= 1.15  # Multi-component agreement bonus
        elif negative_count >= 4:
            final_score *= 1.15  # Multi-component agreement (negative)
        
        return final_score
```

---

## VII. Ước Tính Ảnh Hưởng

```
┌──────────────────────────────────────────────┬────────────┬──────────────┐
│ Component                                    │ Elo Est.   │ Confidence   │
├──────────────────────────────────────────────┼────────────┼──────────────┤
│ EGB-E Evolutionary Graph (emerging opps)     │ +10-20     │ ★★★ Medium   │
│ EHP Exchange Horizon Projector               │ +5-12      │ ★★ Med-Low   │
│ TED Threat Evolution Dynamics                │ +8-18      │ ★★★ Medium   │
│ SAE Synergistic Aftermath                    │ +8-15      │ ★★★ Medium   │
│ HOIT Higher-Order Interactions               │ +5-12      │ ★★ Med-Low   │
│ MTP Material Transform Projector             │ +8-15      │ ★★★ Medium   │
│ CTE Conditional Temporal Engine              │ +10-20     │ ★★★★ High    │
│ ENM Exchange Negotiation Model               │ +10-22     │ ★★★ Medium   │
│ SEA Strategic Exchange Alignment             │ +8-18      │ ★★★ Medium   │
│ GCC Graduated Computation (perf. recovery)   │ +5-10      │ ★★★★ High    │
│ DLE Learning Engine (self-improvement)       │ +5-15      │ ★★★ Medium   │
├──────────────────────────────────────────────┼────────────┼──────────────┤
│ DEE-X total (with heavy overlap)             │ +50-120    │              │
│ DEE original contribution                    │ +50-90     │              │
│ DEE-X additional contribution                │ +30-70     │              │
│ Combined DEE + DEE-X                         │ +80-160    │              │
│ Conservative estimate for DEE-X additions    │ +20-45     │              │
└──────────────────────────────────────────────┴────────────┴──────────────┘

IMPACT BY POSITION TYPE:
┌──────────────────────────────┬────────────────────────────────────────┐
│ Position Type                │ DEE-X Additional Improvement           │
├──────────────────────────────┼────────────────────────────────────────┤
│ Complex middlegame (many     │ +30-60 (synergy + negotiation + align.)│
│  exchange possibilities)     │                                        │
│ Strategic maneuvering        │ +20-40 (alignment + timing + horizon)  │
│ Piece imbalance positions    │ +25-45 (material transform + aftermath)│
│ Attack vs defense            │ +25-50 (threat evolution + negotiation)│
│ Endgame transitions          │ +20-40 (material + timing + alignment) │
│ Sacrifice evaluation         │ +30-55 (tactical + synergy + horizon)  │
│ Tension management           │ +25-45 (negotiation + temporal + TAQ)  │
│ Quiet positional             │ +10-25 (horizon + alignment)           │
└──────────────────────────────┴────────────────────────────────────────┘
```

---

## VIII. Lộ Trình Triển Khai

```
Phase 1 (Month 1-4): Perception Enhancements
├── Implement EGB-E (Evolutionary Exchange Graph)
│   ├── Current graph (reuse DEE gốc)
│   ├── 1-step projection
│   └── Trajectory analysis
├── Implement TED (Threat Evolution Dynamics)
│   ├── Threat lifecycle model
│   ├── Conditional threats
│   └── Threat chains
├── Test: improved exchange landscape understanding
└── Target: +10-20 Elo from perception improvements

Phase 2 (Month 5-8): Analysis Enhancements
├── Implement SAE (Synergistic Aftermath)
│   ├── Combined aftermath evaluation
│   ├── Synergy decomposition
│   └── Synergistic set finder
├── Implement HOIT (Higher-Order Interactions)
│   ├── Enhanced pairwise
│   └── Three-way interactions
├── Implement MTP (Material Transform Projector)
│   ├── Imbalance analysis
│   ├── Endgame projection
│   └── Structure fit
├── Test: more accurate aftermath evaluation
└── Target: +25-40 Elo total

Phase 3 (Month 9-12): Strategy Enhancements
├── Implement CTE (Conditional Temporal Engine)
│   ├── Trigger-based timing
│   ├── Opportunity windows
│   └── Phase-transition timing
├── Implement ENM (Exchange Negotiation Model)
│   ├── Two-sided evaluation
│   ├── Avoidance cost
│   └── Negotiation dynamics
├── Implement SEA (Strategic Exchange Alignment)
│   ├── Plan alignment
│   ├── Piece role analysis
│   └── Structural alignment
├── Test: strategic exchange quality
└── Target: +35-60 Elo total

Phase 4 (Month 13-16): Infrastructure
├── Implement GCC (Graduated Computation)
│   ├── Tier selection logic
│   ├── Per-tier execution
│   └── Cost management
├── Implement DLE (Learning Engine)
│   ├── Weight calibration
│   ├── Pattern learning
│   └── Cross-game persistence
├── Implement DIH (Integration Hub)
│   ├── Score aggregation
│   ├── Component coordination
│   └── Integration with main engine
├── Performance optimization
└── Target: +40-70 Elo total, acceptable NPS

Phase 5 (Month 17-20): Advanced Features
├── Implement EHP (Exchange Horizon Projector)
│   ├── Pattern matching
│   ├── Neural horizon prediction
│   └── Piece lifecycle projection
├── Full system integration
├── Extensive tournament testing
├── Parameter tuning via SPRT
└── Target: Final +20-45 Elo over DEE gốc

Phase 6 (Month 21-24): Integration & Polish
├── Integration with HAMO (move ordering)
├── Integration with UPAD (pruning)
├── Integration with DQRS (quiescence)
├── Integration with CASE (extensions)
├── Integration with PRAW (aspiration)
├── Integration with HAPS (parallel search)
├── Cross-system optimization
└── Target: Complete engine with all systems working together
```

DEE-X mở rộng DEE gốc từ **"đánh giá exchanges tại thời điểm hiện tại"** thành **"hiểu toàn bộ dynamics của exchange landscape"** — bao gồm sự tiến hóa (EGB-E, EHP), tương tác bậc cao (HOIT, SAE), chiến lược timing (CTE), đàm phán hai bên (ENM), liên kết với kế hoạch (SEA), dự đoán biến đổi vật chất (MTP), động lực threat (TED) — tất cả được kiểm soát bởi hệ thống tính toán phân tầng (GCC) và tự cải thiện liên tục (DLE).