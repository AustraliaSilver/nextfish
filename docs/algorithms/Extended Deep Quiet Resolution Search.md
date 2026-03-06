

# DQRS-X: Extended Deep Quiet Resolution Search

---

## I. Phân Tích Điểm Yếu Còn Lại Của DQRS Gốc

### 1.1 Chín Lỗ Hổng Cấu Trúc

```
┌──────────────────────────────────────────────────────────────────────────┐
│                    DQRS REMAINING WEAKNESSES                             │
│                                                                          │
│  ❶ EXCHANGE SEQUENCE BLINDNESS                                          │
│     DQRS xử lý từng capture riêng lẻ. Nhưng exchanges là CHUỖI CÓ     │
│     CẤU TRÚC (NxB, PxN, BxP...) với internal logic riêng. Nếu model   │
│     chuỗi exchange như đơn vị → predict kết quả nhanh hơn, chính xác   │
│     hơn, và phát hiện zwischenzug tốt hơn.                             │
│                                                                          │
│  ❷ THREAT CHAIN IGNORANCE                                               │
│     DQRS liệt kê threats RIÊNG LẺ. Nhưng threats TƯƠNG TÁC: threat A  │
│     enable threat B, resolving threat C creates threat D. Cần MODEL     │
│     threat DAG (directed acyclic graph) để hiểu cascading effects.     │
│                                                                          │
│  ❸ STATIC RESOLUTION SCORING                                            │
│     Resolution score dựa trên hand-crafted features. Có thể sai 20-30% │
│     trong positions phức tạp. Lightweight neural net có thể predict     │
│     tốt hơn nhiều.                                                      │
│                                                                          │
│  ❹ SYMMETRIC DEPTH ALLOCATION                                           │
│     DQRS cấp cùng depth cho cả hai bên. Nhưng thường MỘT BÊN cần      │
│     search sâu hơn (bên tấn công cần verify combo, bên thủ cần tìm    │
│     defense). Asymmetric allocation tiết kiệm 20-30% nodes.            │
│                                                                          │
│  ❺ SACRIFICE BLACK BOX                                                  │
│     SEE < 0 → skip hoặc speculative search. Nhưng không có FRAMEWORK   │
│     để systematically validate sacrifice: "sacrifice này đổi lại gì?"  │
│     Compensation estimation trong DQRS quá thô sơ.                     │
│                                                                          │
│  ❻ QSEARCH TT UNDERUTILIZATION                                         │
│     QSearch positions lặp lại RẤT NHIỀU (transpositions trong exchange │
│     sequences). Nhưng QSearch TT usage rất primitive: chỉ probe/store  │
│     score, không tận dụng structural information.                       │
│                                                                          │
│  ❼ ABRUPT MAIN→QSEARCH BOUNDARY                                        │
│     Chuyển từ main search sang QSearch là BẤT NGỜ: depth=1 → depth=0   │
│     → QSearch. Move types thay đổi đột ngột (all → captures only).     │
│     Cần transition zone mượt hơn.                                       │
│                                                                          │
│  ❽ NO EVAL TRAJECTORY PREDICTION                                        │
│     DQRS đánh giá position tại MỖI NODE độc lập. Không predict eval    │
│     trajectory: "nếu tiếp tục exchange, eval sẽ đi về đâu?" Prediction │
│     cho phép prune sớm hơn, chính xác hơn.                             │
│                                                                          │
│  ❾ NO TACTICAL PATTERN MEMORY                                           │
│     Mỗi QSearch call phân tích từ đầu. Không nhớ "pattern này đã thấy  │
│     trước → kết quả là X". Cross-position pattern matching tiết kiệm   │
│     rất nhiều computation.                                               │
└──────────────────────────────────────────────────────────────────────────┘
```

### 1.2 DQRS-X Extension Map

```
                          ┌──────────────────────┐
                          │       DQRS-X          │
                          │  Extended Architecture │
                          └──────────┬─────────────┘
                                     │
         ┌───────────────────────────┼────────────────────────────┐
         │                           │                            │
    ┌────┴─────┐              ┌─────┴──────┐              ┌─────┴──────┐
    │ Layer 1  │              │  Layer 2   │              │  Layer 3   │
    │ Sequence │              │  Prediction│              │  System    │
    │ & Chain  │              │  & Scoring │              │Integration │
    │ Modeling │              │            │              │            │
    └────┬─────┘              └─────┬──────┘              └─────┬──────┘
         │                          │                           │
    ┌────┴────────┐           ┌────┴──────────┐          ┌────┴──────────┐
    │ Ext 1:      │           │ Ext 4:        │          │ Ext 7:        │
    │ Exchange    │           │ Eval          │          │ Boundary      │
    │ Sequence    │           │ Trajectory    │          │ Smoothing     │
    │ Algebra     │           │ Prediction    │          │               │
    ├─────────────┤           ├───────────────┤          ├───────────────┤
    │ Ext 2:      │           │ Ext 5:        │          │ Ext 8:        │
    │ Threat      │           │ Sacrifice     │          │ QSearch TT    │
    │ Chain       │           │ Validation    │          │ Optimization  │
    │ DAG         │           │ Framework     │          │               │
    ├─────────────┤           ├───────────────┤          ├───────────────┤
    │ Ext 3:      │           │ Ext 6:        │          │ Ext 9:        │
    │ Asymmetric  │           │ Tactical      │          │ Defensive     │
    │ Depth       │           │ Pattern       │          │ Resource      │
    │ Allocation  │           │ Memory        │          │ Counting      │
    └─────────────┘           └───────────────┘          └───────────────┘
```

---

## II. Extension 1: Exchange Sequence Algebra (ESA)

### 2.1 Vấn Đề Cốt Lõi

```
DQRS gốc xử lý exchange như chuỗi captures riêng lẻ:

  NxBd5, PxNd5, BxPd5, RxBd5, ...
  
  Mỗi capture = 1 recursive call → branch ra
  Mỗi branch có thể branch thêm → tree explosion
  
  Nhưng exchange trên D5 là MỘT ĐƠN VỊ LOGIC:
  
  ┌────────────────────────────────────────────────┐
  │ Exchange on d5:                                │
  │   N(c3) captures B(d5)    [+300]               │
  │   P(e6) captures N(d5)    [+300, net: -300]    │
  │   B(f3) captures P(d5)    [+100, net: +100]    │
  │   R(d8) captures B(d5)    [+300, net: -200]    │
  │                                                │
  │   Final: White gains B(300), loses N(300)+B(300)│
  │   Wait — that's SEE territory                  │
  │                                                │
  │   BUT: What if between P×N and B×P, White      │
  │   plays Qh5+? (zwischenzug)                    │
  │   The WHOLE exchange sequence changes!         │
  │                                                │
  │   ESA models the exchange AS A STRUCTURE       │
  │   with insertion points for zwischenzug        │
  └────────────────────────────────────────────────┘

Key insight: SEE calculates exchange result assuming strict alternation.
ESA models exchanges as ALGEBRAIC SEQUENCES with:
  - Defined participants and order
  - Identified insertion points (where non-capture can insert)
  - Pre-computed result for each deviation
  - Optimized search: only search DEVIATIONS from predicted sequence
```

### 2.2 Implementation

```python
class ExchangeSequenceAlgebra:
    """Model exchanges as structured algebraic sequences"""
    
    def __init__(self):
        self.sequence_cache = LRUCache(max_size=4096)
    
    def analyze_exchange(self, position, initial_capture):
        """Analyze complete exchange sequence starting from capture"""
        
        target_square = initial_capture.to_square
        
        # ═══ STEP 1: BUILD LINEAR SEQUENCE ═══
        # Predict the "default" exchange sequence (like extended SEE)
        
        sequence = ExchangeSequence(target_square=target_square)
        
        sim_position = position.copy()
        current_side = position.side_to_move
        
        # Build sequence of captures on target square
        while True:
            # Find least valuable attacker for current side
            attacker = find_least_valuable_attacker(
                sim_position, target_square, current_side
            )
            
            if attacker is None:
                break  # No more captures possible
            
            captured_piece = sim_position.piece_at(target_square)
            if captured_piece is None:
                break
            
            capture_move = Move(attacker, target_square)
            
            step = ExchangeStep(
                move=capture_move,
                capturer=attacker,
                captured=captured_piece,
                material_delta=piece_value(captured_piece.type),
                side=current_side,
                step_index=len(sequence.steps),
            )
            
            sequence.steps.append(step)
            
            # Simulate capture
            sim_position.make_move(capture_move)
            current_side = current_side ^ 1
            
            # Safety limit
            if len(sequence.steps) >= 12:
                break
        
        # ═══ STEP 2: COMPUTE MATERIAL BALANCE AT EACH POINT ═══
        
        running_balance = 0  # From initial capturer's perspective
        
        for i, step in enumerate(sequence.steps):
            if i % 2 == 0:
                # Our capture: we gain
                running_balance += step.material_delta
            else:
                # Their capture: we lose
                running_balance -= step.material_delta
            
            step.cumulative_balance = running_balance
            
            # Optimal stopping point for each side
            # Side to move CAN choose to stop the exchange
            step.can_stop = True
        
        # ═══ STEP 3: FIND OPTIMAL STOPPING POINTS ═══
        
        sequence.optimal_result = self.find_optimal_stopping(sequence)
        
        # ═══ STEP 4: IDENTIFY INSERTION POINTS ═══
        
        sequence.insertion_points = self.find_insertion_points(
            position, sequence
        )
        
        # ═══ STEP 5: IDENTIFY DEVIATIONS ═══
        
        sequence.possible_deviations = self.find_deviations(
            position, sequence
        )
        
        return sequence
    
    def find_optimal_stopping(self, sequence):
        """Find optimal point for each side to stop exchanging"""
        
        # Minimax over the exchange sequence
        # Each side can choose to stop at their turn
        
        n = len(sequence.steps)
        if n == 0:
            return 0
        
        # Work backwards
        # best[i] = best result (for initial capturer) if exchange
        #           continues from step i onward
        
        best = [0] * (n + 1)
        best[n] = sequence.steps[-1].cumulative_balance if n > 0 else 0
        
        for i in range(n - 1, -1, -1):
            step = sequence.steps[i]
            
            # Value if we STOP here
            stop_value = sequence.steps[i-1].cumulative_balance if i > 0 else 0
            
            # Value if we CONTINUE
            continue_value = best[i + 1]
            
            if i % 2 == 0:
                # Our turn: we want to MAXIMIZE
                best[i] = max(stop_value, continue_value)
            else:
                # Their turn: they want to MINIMIZE (from our perspective)
                best[i] = min(stop_value, continue_value)
        
        return best[0]
    
    def find_insertion_points(self, position, sequence):
        """Find points where zwischenzug can be inserted"""
        
        insertion_points = []
        
        sim_position = position.copy()
        
        for i, step in enumerate(sequence.steps):
            # Before each capture in the sequence,
            # check if the capturing side has a STRONGER move
            
            current_side = step.side
            
            # Look for non-capture moves that create bigger threats
            zwischenzug_candidates = self.find_zwischenzug_at_point(
                sim_position, step, sequence, i
            )
            
            if zwischenzug_candidates:
                insertion_points.append(InsertionPoint(
                    index=i,
                    candidates=zwischenzug_candidates,
                    exchange_value_if_skipped=step.cumulative_balance,
                ))
            
            # Advance simulation
            sim_position.make_move(step.move)
        
        return insertion_points
    
    def find_zwischenzug_at_point(self, position, step, sequence, index):
        """Find zwischenzug candidates at a specific point in exchange"""
        
        candidates = []
        current_side = step.side
        
        # What is the value of continuing the exchange?
        exchange_continuation_value = (
            sequence.optimal_result if index == 0 
            else sequence.steps[index - 1].cumulative_balance
        )
        
        # Look for moves that create threats BIGGER than exchange gain
        for move in generate_threatening_moves_fast(position, current_side):
            # Skip captures on the exchange square (part of sequence)
            if move.to_square == sequence.target_square:
                continue
            
            threat_value = quick_threat_estimate(position, move)
            
            # Zwischenzug worthwhile if:
            # 1. Creates threat bigger than what we'd gain from exchange
            # 2. Opponent MUST respond to this threat
            # 3. After response, we can still continue exchange
            
            if threat_value > abs(exchange_continuation_value) + 50:
                # Check if threat is forcing (opponent must respond)
                is_forcing = is_move_forcing(position, move)
                
                if is_forcing:
                    # Check if exchange can resume after response
                    can_resume = self.can_exchange_resume(
                        position, move, sequence, index
                    )
                    
                    candidates.append(ZwischenzugCandidate(
                        move=move,
                        threat_value=threat_value,
                        is_forcing=True,
                        can_resume=can_resume,
                        net_gain_estimate=threat_value - exchange_continuation_value,
                    ))
        
        # Sort by estimated gain
        candidates.sort(key=lambda c: -c.net_gain_estimate)
        
        return candidates[:3]  # Top 3 candidates
    
    def can_exchange_resume(self, position, zwischenzug_move, 
                             sequence, insertion_index):
        """Check if exchange can resume after zwischenzug + response"""
        
        # After zwischenzug, opponent responds
        # Then can we still capture on exchange square?
        
        sim = position.copy()
        sim.make_move(zwischenzug_move)
        
        # Opponent's best response to our threat
        responses = generate_threat_responses(sim, zwischenzug_move)
        
        for response in responses[:3]:
            sim2 = sim.copy()
            sim2.make_move(response)
            
            # Can we still capture on exchange square?
            original_capture = sequence.steps[insertion_index].move
            if is_legal(sim2, original_capture):
                return True
            
            # Or a similar capture?
            for capture in generate_captures_on_square(
                sim2, sequence.target_square
            ):
                if is_legal(sim2, capture):
                    return True
        
        return False
    
    def find_deviations(self, position, sequence):
        """Find possible deviations from predicted sequence"""
        
        deviations = []
        sim_position = position.copy()
        
        for i, step in enumerate(sequence.steps):
            # At each step, check if the capturing side has
            # a DIFFERENT capture that's better
            
            current_side = step.side
            
            # All captures for this side (not just on exchange square)
            all_captures = generate_captures(sim_position)
            
            for capture in all_captures:
                # Skip the predicted capture
                if capture == step.move:
                    continue
                
                # Skip captures already in the sequence
                if capture.to_square == sequence.target_square:
                    continue
                
                # Is this alternative capture better?
                see_val = see(sim_position, capture)
                
                # Compare: continuing exchange vs this capture
                exchange_remaining = (
                    sequence.optimal_result - 
                    (step.cumulative_balance if i > 0 else 0)
                )
                
                if see_val > exchange_remaining + 30:
                    deviations.append(ExchangeDeviation(
                        at_step=i,
                        alternative_move=capture,
                        see_value=see_val,
                        exchange_remaining=exchange_remaining,
                        advantage=see_val - exchange_remaining,
                    ))
            
            sim_position.make_move(step.move)
        
        deviations.sort(key=lambda d: -d.advantage)
        return deviations[:5]


class ExchangeOptimizedSearch:
    """Use ESA to optimize QSearch for exchange positions"""
    
    def __init__(self, esa):
        self.esa = esa
    
    def search_exchange(self, position, initial_capture, alpha, beta):
        """Optimized search for exchange sequence"""
        
        # Step 1: Analyze exchange
        sequence = self.esa.analyze_exchange(position, initial_capture)
        
        # Step 2: If no insertions or deviations → return SEE-like result
        if (not sequence.insertion_points and 
            not sequence.possible_deviations):
            return sequence.optimal_result
        
        # Step 3: Search only deviations and insertion points
        best_score = sequence.optimal_result
        
        # Search zwischenzug candidates
        for insertion in sequence.insertion_points:
            for candidate in insertion.candidates:
                position.make_move(candidate.move)
                
                # Opponent responds to zwischenzug
                score = -self.search_zwischenzug_continuation(
                    position, -beta, -alpha, sequence, insertion
                )
                
                position.unmake_move(candidate.move)
                
                if score > best_score:
                    best_score = score
                if score > alpha:
                    alpha = score
                if score >= beta:
                    return beta
        
        # Search deviations
        for deviation in sequence.possible_deviations:
            # Only search if deviation seems genuinely better
            if deviation.advantage > 20:
                position.make_move(deviation.alternative_move)
                
                score = -dqrs(position, -beta, -alpha, 4)
                
                position.unmake_move(deviation.alternative_move)
                
                if score > best_score:
                    best_score = score
                if score > alpha:
                    alpha = score
                if score >= beta:
                    return beta
        
        return best_score
    
    def search_zwischenzug_continuation(self, position, alpha, beta,
                                          original_sequence, insertion):
        """Search after zwischenzug: opponent response + exchange resume"""
        
        best_score = -INFINITY
        
        # Generate opponent responses to our zwischenzug
        for move in generate_legal_moves(position):
            position.make_move(move)
            
            # After response, can exchange resume?
            can_resume = self.esa.can_exchange_resume(
                position, move, original_sequence, insertion.index
            )
            
            if can_resume:
                # Resume exchange from where we left off
                resume_capture = find_resume_capture(
                    position, original_sequence, insertion.index
                )
                if resume_capture:
                    score = -self.search_exchange(
                        position, resume_capture, -beta, -alpha
                    )
                else:
                    score = -dqrs(position, -beta, -alpha, 3)
            else:
                # Exchange can't resume → normal QSearch
                score = -dqrs(position, -beta, -alpha, 3)
            
            position.unmake_move(move)
            
            if score > best_score:
                best_score = score
            if score > alpha:
                alpha = score
            if score >= beta:
                return beta
        
        return best_score
```

---

## III. Extension 2: Threat Chain DAG

### 3.1 Vấn Đề

```
DQRS gốc: threats là danh sách phẳng

  Threat 1: Nf3 attacks Qd4
  Threat 2: Bb5 pins Nc6
  Threat 3: Re1 x-rays Qe7
  
Thực tế: threats TƯƠNG TÁC theo chuỗi

  Threat Chain A:
    Nf3 attacks Qd4 → Qd4 must move → 
    Qd4 was defending Nc6 → Nc6 now undefended →
    Bb5 can capture Nc6 (free piece!)
  
  Threat Chain B:
    Re1 pins Ne5 → Ne5 can't move →
    d4 advance attacks Ne5 → Ne5 is lost (can't move, pinned)
  
  Key insight: Resolving threat 1 may CREATE threats 4, 5, 6
  DQRS chỉ thấy snapshot → misses cascading effects
  
  DAG captures: "if A resolves, what new threats emerge?"
```

### 3.2 Implementation

```python
class ThreatChainDAG:
    """Model threats as Directed Acyclic Graph of dependencies"""
    
    def __init__(self):
        self.nodes = {}      # threat_id → ThreatNode
        self.edges = []      # (source, target, edge_type)
        self.root_threats = []
    
    def build_dag(self, position, threat_inventory):
        """Build threat dependency DAG"""
        
        self.nodes.clear()
        self.edges.clear()
        
        # ═══ STEP 1: CREATE NODES FROM THREATS ═══
        
        all_threats = threat_inventory.all_threats()
        
        for i, threat in enumerate(all_threats):
            node = ThreatNode(
                id=i,
                threat=threat,
                depth=0,       # Will be computed
                resolved=False,
                children=[],
                parents=[],
            )
            self.nodes[i] = node
        
        # ═══ STEP 2: FIND DEPENDENCIES ═══
        
        for i, threat_a in enumerate(all_threats):
            for j, threat_b in enumerate(all_threats):
                if i == j:
                    continue
                
                dep = self.find_dependency(
                    position, threat_a, threat_b
                )
                
                if dep:
                    edge = ThreatEdge(
                        source=i,
                        target=j,
                        type=dep.type,
                        strength=dep.strength,
                    )
                    self.edges.append(edge)
                    self.nodes[i].children.append(j)
                    self.nodes[j].parents.append(i)
        
        # ═══ STEP 3: FIND CASCADING THREATS ═══
        # Threats that DON'T exist yet but WOULD exist 
        # if certain threats are resolved
        
        cascading = self.find_cascading_threats(position, all_threats)
        
        for cascade in cascading:
            # Add cascading threat as new node
            new_id = len(self.nodes)
            node = ThreatNode(
                id=new_id,
                threat=cascade.new_threat,
                depth=cascade.depth,
                resolved=False,
                is_cascading=True,
                trigger_threat=cascade.trigger_id,
            )
            self.nodes[new_id] = node
            
            # Edge: resolving trigger → creates this
            edge = ThreatEdge(
                source=cascade.trigger_id,
                target=new_id,
                type='creates_on_resolution',
                strength=cascade.probability,
            )
            self.edges.append(edge)
        
        # ═══ STEP 4: COMPUTE DEPTHS ═══
        
        self.compute_depths()
        
        # ═══ STEP 5: IDENTIFY ROOT THREATS ═══
        
        self.root_threats = [
            nid for nid, node in self.nodes.items()
            if not node.parents
        ]
        
        return self
    
    def find_dependency(self, position, threat_a, threat_b):
        """Find if threat_a depends on / enables threat_b"""
        
        # ── Type 1: DEFENDER REMOVAL ──
        # Resolving threat_a (capturing defender) enables threat_b
        
        if threat_a.type == THREAT_CAPTURE:
            target_piece = threat_a.target
            
            # Does target_piece defend something that threat_b attacks?
            if threat_b.type == THREAT_CAPTURE:
                defended_square = threat_b.target.square
                
                if position.is_defending(
                    target_piece, defended_square
                ):
                    return Dependency(
                        type='defender_removal',
                        strength=0.8,
                        description=f'Capturing {target_piece} removes '
                                    f'defense of {defended_square}'
                    )
        
        # ── Type 2: LINE OPENING ──
        # Moving/capturing piece opens line for another threat
        
        if threat_a.type in [THREAT_CAPTURE, THREAT_CHECK]:
            moving_piece_sq = threat_a.source.square if threat_a.source else None
            
            if moving_piece_sq:
                # Does removing piece from this square open line?
                opened_lines = find_opened_lines(
                    position, moving_piece_sq
                )
                
                for line in opened_lines:
                    # Does threat_b use this line?
                    if threat_b.source and line.contains(threat_b.source.square):
                        return Dependency(
                            type='line_opening',
                            strength=0.7,
                            description=f'Moving piece from {moving_piece_sq} '
                                        f'opens line for {threat_b}'
                        )
        
        # ── Type 3: SQUARE CONTROL ──
        # Resolving threat_a gives control of square needed for threat_b
        
        if (threat_b.type == THREAT_FORK and 
            threat_b.details and 'fork_square' in threat_b.details):
            
            fork_sq = threat_b.details['fork_square']
            
            if threat_a.type == THREAT_CAPTURE:
                # Does capturing remove control of fork square?
                captured = threat_a.target
                if position.controls_square(captured, fork_sq):
                    return Dependency(
                        type='square_control',
                        strength=0.6,
                    )
        
        # ── Type 4: MUTUAL EXCLUSION ──
        # Resolving threat_a prevents threat_b (same piece involved)
        
        if (threat_a.source and threat_b.source and
            threat_a.source.square == threat_b.source.square):
            # Same piece: can't execute both threats
            return Dependency(
                type='mutual_exclusion',
                strength=0.9,
            )
        
        return None
    
    def find_cascading_threats(self, position, existing_threats):
        """Find threats that would emerge if existing threats resolve"""
        
        cascading = []
        
        for i, threat in enumerate(existing_threats):
            if threat.type != THREAT_CAPTURE:
                continue
            
            # Simulate resolving this threat
            sim = position.copy()
            
            # Case: threat resolves by moving target away
            target = threat.target
            safe_squares = find_safe_squares(sim, target)
            
            for safe_sq in safe_squares[:3]:
                sim2 = sim.copy()
                sim2.make_move(Move(target, safe_sq))
                
                # What new threats exist after target moved?
                new_threats = quick_threat_scan(sim2)
                
                for new_threat in new_threats:
                    # Is this genuinely new (not in existing threats)?
                    if not self.threat_exists(new_threat, existing_threats):
                        cascading.append(CascadingThreat(
                            trigger_id=i,
                            trigger_resolution='move_away',
                            new_threat=new_threat,
                            depth=1,
                            probability=0.3 / len(safe_squares),
                        ))
            
            # Case: threat resolves by capturing attacker
            attacker = threat.source
            if attacker:
                capture_move = Move(
                    find_capturer(sim, attacker),
                    attacker.square
                )
                if is_legal(sim, capture_move):
                    sim3 = sim.copy()
                    sim3.make_move(capture_move)
                    
                    new_threats = quick_threat_scan(sim3)
                    
                    for new_threat in new_threats:
                        if not self.threat_exists(new_threat, existing_threats):
                            cascading.append(CascadingThreat(
                                trigger_id=i,
                                trigger_resolution='capture_attacker',
                                new_threat=new_threat,
                                depth=1,
                                probability=0.5,
                            ))
        
        return cascading
    
    def compute_depths(self):
        """Compute depth of each node in DAG (topological sort)"""
        
        # BFS from root threats
        from collections import deque
        
        queue = deque()
        for root_id in self.root_threats:
            self.nodes[root_id].depth = 0
            queue.append(root_id)
        
        while queue:
            nid = queue.popleft()
            node = self.nodes[nid]
            
            for child_id in node.children:
                child = self.nodes[child_id]
                new_depth = node.depth + 1
                if new_depth > child.depth:
                    child.depth = new_depth
                    queue.append(child_id)
    
    def compute_chain_value(self):
        """Compute total value of threat chains"""
        
        total = 0.0
        
        # Chains that cascade = value multiplier
        for nid, node in self.nodes.items():
            base_value = node.threat.severity
            
            # Amplify by chain depth
            depth_multiplier = 1.0 + node.depth * 0.2
            
            # Discount cascading (uncertain) threats
            if node.is_cascading:
                base_value *= node.trigger_probability
            
            total += base_value * depth_multiplier
        
        return total
    
    def get_resolution_priority(self):
        """Order in which threats should be resolved"""
        
        # Priority: resolve ROOT threats first (they enable others)
        # Within same depth: resolve highest-severity first
        
        priorities = []
        
        for nid, node in self.nodes.items():
            if node.is_cascading:
                continue  # Don't try to resolve cascading (they don't exist yet)
            
            # Priority score: higher = resolve first
            priority = (
                node.threat.severity * node.threat.urgency
                + len(node.children) * 50  # More children = more enabling
                - node.depth * 20          # Deeper = less urgent
            )
            
            priorities.append((nid, priority))
        
        priorities.sort(key=lambda x: -x[1])
        return priorities
    
    def estimate_resolution_cost(self):
        """Estimate how many QSearch plies needed to resolve all chains"""
        
        max_chain_depth = max(
            (node.depth for node in self.nodes.values()), default=0
        )
        
        # Each chain depth ≈ 2 QSearch plies (threat + response)
        estimated_plies = max_chain_depth * 2 + 2
        
        # Branching from multiple chains
        chain_count = len(self.root_threats)
        branching_penalty = chain_count * 0.5
        
        return int(estimated_plies + branching_penalty)


class ThreatChainIntegration:
    """Integrate threat chain DAG into DQRS"""
    
    def adjust_resolution_score(self, base_resolution, dag):
        """Adjust resolution score based on threat chains"""
        
        chain_value = dag.compute_chain_value()
        chain_depth = max(
            (n.depth for n in dag.nodes.values()), default=0
        )
        
        # Deep chains = more unresolved
        chain_penalty = min(chain_depth * 0.1, 0.4)
        
        # High chain value = more dangerous
        value_penalty = min(chain_value / 3000.0, 0.3)
        
        adjusted = base_resolution - chain_penalty - value_penalty
        
        return clamp(adjusted, 0.0, 1.0)
    
    def prioritize_moves_by_chain(self, moves, dag, position):
        """Reorder moves based on threat chain priorities"""
        
        priorities = dag.get_resolution_priority()
        priority_map = {nid: p for nid, p in priorities}
        
        scored_moves = []
        
        for move in moves:
            chain_score = 0.0
            
            # Does this move resolve a high-priority threat?
            for nid, node in dag.nodes.items():
                if node.resolved or node.is_cascading:
                    continue
                
                resolves = does_move_resolve_threat(
                    position, move, node.threat
                )
                
                if resolves:
                    chain_score += priority_map.get(nid, 0) * 0.01
            
            # Does this move CREATE a new threat in the chain?
            creates = does_move_create_chain_threat(
                position, move, dag
            )
            if creates:
                chain_score += 50
            
            scored_moves.append((move, chain_score))
        
        # Sort by chain score (descending), then by original order
        scored_moves.sort(key=lambda x: -x[1])
        
        return [m for m, s in scored_moves]
```

---

## IV. Extension 3: Asymmetric Depth Allocation

### 4.1 Core Concept

```
Standard QSearch: same depth for both sides
  Depth budget = 6 → both attacker and defender get 3 plies each

Reality: ASYMMETRIC needs

┌────────────────────────────────────────────────────────────────┐
│ Scenario A: We're ATTACKING (have initiative)                  │
│   - We need MORE depth to verify our combination works         │
│   - Opponent needs LESS depth (just find best defense)         │
│   - Optimal: we get 4 plies, opponent gets 2                  │
│                                                                │
│ Scenario B: We're DEFENDING (opponent has initiative)          │
│   - We need MORE depth to find all defensive resources         │
│   - Opponent needs LESS depth (their threats are clear)        │
│   - Optimal: we get 4 plies, opponent gets 2                  │
│                                                                │
│ Scenario C: BALANCED position                                  │
│   - Both sides need similar depth                              │
│   - Optimal: symmetric allocation                              │
│                                                                │
│ Key observation: the side with MORE TACTICAL OPTIONS            │
│ needs MORE depth to explore them all                           │
└────────────────────────────────────────────────────────────────┘
```

### 4.2 Implementation

```python
class AsymmetricDepthAllocator:
    """Allocate different QSearch depth for each side"""
    
    def compute_allocation(self, position, resolution, total_budget):
        """Compute depth allocation for each side"""
        
        stm = position.side_to_move
        opp = stm ^ 1
        
        # ═══ FACTOR 1: INITIATIVE ═══
        
        initiative = self.measure_initiative(position, resolution)
        # initiative > 0: stm has initiative
        # initiative < 0: opponent has initiative
        
        # ═══ FACTOR 2: TACTICAL DENSITY PER SIDE ═══
        
        stm_tactical_options = count_tactical_options(position, stm)
        opp_tactical_options = count_tactical_options(position, opp)
        
        total_options = stm_tactical_options + opp_tactical_options + 1
        stm_option_ratio = stm_tactical_options / total_options
        
        # ═══ FACTOR 3: THREAT COMPLEXITY PER SIDE ═══
        
        stm_threat_complexity = sum(
            t.severity for t in resolution.threat_inventory.all_threats()
            if t.type in [THREAT_CAPTURE, THREAT_FORK, THREAT_CHECK]
            and t.source and t.source.side == opp  # Threats AGAINST stm
        )
        
        opp_threat_complexity = sum(
            t.severity for t in resolution.threat_inventory.all_threats()
            if t.type in [THREAT_CAPTURE, THREAT_FORK, THREAT_CHECK]
            and t.source and t.source.side == stm  # Threats AGAINST opp
        )
        
        total_complexity = stm_threat_complexity + opp_threat_complexity + 1
        stm_defense_need = stm_threat_complexity / total_complexity
        
        # ═══ FACTOR 4: PIECE ACTIVITY DIFFERENTIAL ═══
        
        stm_activity = compute_piece_activity(position, stm)
        opp_activity = compute_piece_activity(position, opp)
        
        activity_ratio = stm_activity / max(stm_activity + opp_activity, 1)
        
        # ═══ COMBINE ═══
        
        # Score > 0.5: stm needs more depth
        # Score < 0.5: opponent needs more depth
        
        stm_need = (
            stm_option_ratio * 0.3 +      # More options → more depth
            stm_defense_need * 0.3 +       # More threats against us → more depth
            activity_ratio * 0.2 +          # More active → more to explore
            (0.5 + initiative * 0.1) * 0.2  # Initiative means more to verify
        )
        
        stm_need = clamp(stm_need, 0.25, 0.75)
        
        # ═══ ALLOCATE ═══
        
        stm_depth = int(total_budget * stm_need + 0.5)
        opp_depth = total_budget - stm_depth
        
        # Ensure minimums
        stm_depth = max(stm_depth, 2)
        opp_depth = max(opp_depth, 2)
        
        return DepthAllocation(
            stm_depth=stm_depth,
            opp_depth=opp_depth,
            stm_need_ratio=stm_need,
            initiative=initiative,
        )
    
    def measure_initiative(self, position, resolution):
        """Measure which side has initiative (-1 to +1)"""
        
        stm = position.side_to_move
        
        # Check threats
        stm_threats = sum(
            1 for t in resolution.threat_inventory.all_threats()
            if t.source and t.source.side == stm and t.severity > 100
        )
        
        opp_threats = sum(
            1 for t in resolution.threat_inventory.all_threats()
            if t.source and t.source.side != stm and t.severity > 100
        )
        
        if stm_threats + opp_threats == 0:
            return 0.0
        
        initiative = (stm_threats - opp_threats) / (stm_threats + opp_threats)
        
        # Bonus for checks available
        if can_give_check(position, stm):
            initiative += 0.2
        if can_give_check(position, stm ^ 1):
            initiative -= 0.2
        
        return clamp(initiative, -1.0, 1.0)
    
    def apply_asymmetric_depth(self, position, move, allocation):
        """Get effective depth for a move based on allocation"""
        
        moving_side = position.side_to_move
        
        if moving_side == allocation.stm_side:
            return allocation.stm_depth
        else:
            return allocation.opp_depth
```

---

## V. Extension 4: Eval Trajectory Prediction

### 5.1 Core Idea

```
Current DQRS: evaluate each node independently
  Node A: eval = +150
  Node B (after capture): eval = +80
  Node C (after recapture): eval = +160
  
  No connection between these evals — each is a fresh call

Trajectory Prediction: predict WHERE eval is heading
  
  Observation sequence in this QSearch branch:
    Depth 0: +150
    Depth 1: +80 (big drop — capture happened)
    Depth 2: +160 (recovery — recapture)
    Depth 3: +140 (settling)
    
  Trajectory model predicts:
    Depth 4: ~+145 (converging to ~+145)
    Depth 5: ~+144 (almost converged)
    
  → Can STOP at depth 3 because we PREDICT convergence
  → Saves 2 plies of search!

Also useful for:
  - Delta pruning: use predicted trajectory instead of static delta
  - Aspiration hints: QSearch trajectory informs aspiration bounds
  - Explosion detection: diverging trajectory = tactical storm
```

### 5.2 Implementation

```python
class EvalTrajectoryPredictor:
    """Predict eval trajectory through QSearch"""
    
    def __init__(self):
        self.trajectory = []  # (depth, eval) pairs
        self.model = TrajectoryModel()
    
    def record_eval(self, depth, eval_score, move_type):
        """Record an eval observation"""
        
        self.trajectory.append(TrajectoryPoint(
            depth=depth,
            eval=eval_score,
            move_type=move_type,  # 'capture', 'check', 'quiet'
            timestamp=len(self.trajectory),
        ))
    
    def predict_convergence(self, current_depth):
        """Predict where eval is converging to"""
        
        if len(self.trajectory) < 3:
            return None
        
        recent = self.trajectory[-6:]
        evals = [p.eval for p in recent]
        
        # ═══ METHOD 1: DAMPED OSCILLATION MODEL ═══
        # Exchanges cause oscillation: gain, lose, gain, lose
        # Model: eval(d) = E_final + A * (-1)^d * exp(-λd)
        
        if len(evals) >= 4:
            # Detect oscillation
            deltas = [evals[i] - evals[i-1] for i in range(1, len(evals))]
            
            sign_changes = sum(
                1 for i in range(1, len(deltas))
                if deltas[i] * deltas[i-1] < 0
            )
            
            if sign_changes >= len(deltas) * 0.6:
                # Oscillating → fit damped model
                
                # Estimate E_final as weighted average
                # More recent → more weight
                weights = [0.7 ** (len(evals) - i - 1) for i in range(len(evals))]
                total_weight = sum(weights)
                e_final = sum(e * w for e, w in zip(evals, weights)) / total_weight
                
                # Estimate damping
                amplitudes = [abs(d) for d in deltas]
                if len(amplitudes) >= 2 and amplitudes[0] > 1:
                    damping = amplitudes[-1] / amplitudes[0]
                else:
                    damping = 0.5
                
                # Current amplitude
                current_amplitude = abs(evals[-1] - e_final)
                
                # Predicted future amplitude
                future_amplitude = current_amplitude * damping
                
                return TrajectoryPrediction(
                    converge_value=e_final,
                    current_amplitude=current_amplitude,
                    future_amplitude=future_amplitude,
                    damping=damping,
                    confidence=min(0.9, 0.5 + len(evals) * 0.1),
                    type='damped_oscillation',
                    converged=(future_amplitude < 10),
                )
        
        # ═══ METHOD 2: MONOTONE CONVERGENCE ═══
        # Eval moving in one direction, decelerating
        
        if len(evals) >= 3:
            # Check if monotone
            increasing = all(evals[i] >= evals[i-1] - 5 
                           for i in range(1, len(evals)))
            decreasing = all(evals[i] <= evals[i-1] + 5 
                           for i in range(1, len(evals)))
            
            if increasing or decreasing:
                # Fit: eval(d) = E_final - C * exp(-λd)
                
                deltas = [abs(evals[i] - evals[i-1]) 
                         for i in range(1, len(evals))]
                
                # Rate of change decreasing → converging
                if len(deltas) >= 2 and deltas[0] > 1:
                    deceleration = deltas[-1] / deltas[0]
                    
                    if deceleration < 0.8:
                        # Decelerating → extrapolate
                        remaining_change = deltas[-1] * deceleration / (1 - deceleration + 0.01)
                        
                        if increasing:
                            e_final = evals[-1] + remaining_change
                        else:
                            e_final = evals[-1] - remaining_change
                        
                        return TrajectoryPrediction(
                            converge_value=e_final,
                            current_amplitude=deltas[-1],
                            future_amplitude=deltas[-1] * deceleration,
                            damping=deceleration,
                            confidence=min(0.8, 0.4 + len(evals) * 0.1),
                            type='monotone_convergence',
                            converged=(deltas[-1] < 8),
                        )
        
        # ═══ METHOD 3: NO CLEAR PATTERN ═══
        
        return TrajectoryPrediction(
            converge_value=evals[-1],
            current_amplitude=max(abs(d) for d in deltas) if len(self.trajectory) > 1 else 50,
            future_amplitude=50,
            damping=1.0,
            confidence=0.3,
            type='unknown',
            converged=False,
        )
    
    def should_stop_search(self, current_depth, alpha, beta):
        """Should QSearch stop based on trajectory?"""
        
        prediction = self.predict_convergence(current_depth)
        
        if prediction is None:
            return False
        
        # Condition 1: Converged AND predicted value within [alpha, beta]
        if (prediction.converged and 
            prediction.confidence > 0.7 and
            alpha < prediction.converge_value < beta):
            return True
        
        # Condition 2: Future amplitude too small to matter
        if (prediction.future_amplitude < 5 and 
            prediction.confidence > 0.6):
            return True
        
        # Condition 3: Predicted value WAY beyond beta
        if (prediction.converge_value > beta + 100 and
            prediction.confidence > 0.7):
            return True  # Will cutoff anyway
        
        return False
    
    def get_delta_pruning_margin(self, current_depth):
        """Use trajectory to set adaptive delta pruning margin"""
        
        prediction = self.predict_convergence(current_depth)
        
        if prediction is None:
            return 200  # Default margin
        
        # If converging → tighter margin (less room for improvement)
        if prediction.converged:
            return max(50, int(prediction.future_amplitude * 2))
        
        # If oscillating with known amplitude
        if prediction.type == 'damped_oscillation':
            return max(100, int(prediction.current_amplitude * 1.5))
        
        # Unknown → conservative
        return 250
    
    def detect_divergence(self, current_depth):
        """Detect if eval is DIVERGING (tactical storm)"""
        
        if len(self.trajectory) < 4:
            return False
        
        recent = self.trajectory[-4:]
        evals = [p.eval for p in recent]
        
        deltas = [abs(evals[i] - evals[i-1]) for i in range(1, len(evals))]
        
        # Increasing amplitude → diverging
        if all(deltas[i] > deltas[i-1] * 1.2 for i in range(1, len(deltas))):
            return True
        
        # Very large swings → tactical storm
        if max(deltas) > 300:
            return True
        
        return False
```

---

## VI. Extension 5: Sacrifice Validation Framework

### 6.1 The Problem

```
Current handling of sacrifices in QSearch:

  SEE(BxPf7+) = -200 (lose bishop for pawn)
  
  DQRS: "speculative capture, estimate compensation"
  Compensation estimate: +150 (king exposed + attack)
  Net: -200 + 150 = -50 → SKIP (below threshold)
  
  Reality: BxPf7+ leads to forced mate in 8!
  
  Problem: compensation estimation is TOO CRUDE
  
  Need: SYSTEMATIC framework to:
  1. CLASSIFY sacrifice type
  2. IDENTIFY specific compensation
  3. VERIFY compensation with targeted search
  4. DECIDE: search full depth, reduced depth, or skip
```

### 6.2 Implementation

```python
class SacrificeValidationFramework:
    """Systematic framework for sacrifice evaluation in QSearch"""
    
    # Sacrifice taxonomy
    SACRIFICE_TYPES = {
        'king_attack': {
            'description': 'Sacrifice to expose/attack king',
            'typical_compensation': ['king_safety_drop', 'check_sequence',
                                      'mate_threat'],
            'verification_depth': 6,
        },
        'piece_activity': {
            'description': 'Sacrifice for superior piece activity',
            'typical_compensation': ['centralization', 'outpost',
                                      'domination'],
            'verification_depth': 4,
        },
        'pawn_structure': {
            'description': 'Sacrifice to damage pawn structure',
            'typical_compensation': ['isolated_pawn', 'doubled_pawns',
                                      'passed_pawn_creation'],
            'verification_depth': 4,
        },
        'development': {
            'description': 'Gambit for development lead',
            'typical_compensation': ['tempo_gain', 'piece_development',
                                      'initiative'],
            'verification_depth': 5,
        },
        'clearance': {
            'description': 'Sacrifice to clear square/line',
            'typical_compensation': ['square_control', 'line_opening',
                                      'battery_creation'],
            'verification_depth': 4,
        },
        'deflection': {
            'description': 'Sacrifice to deflect defender',
            'typical_compensation': ['undefended_piece', 'mate_access',
                                      'promotion_path'],
            'verification_depth': 5,
        },
        'destruction': {
            'description': 'Sacrifice to destroy king shelter',
            'typical_compensation': ['pawn_shield_gone', 'open_file',
                                      'king_exposed'],
            'verification_depth': 6,
        },
    }
    
    def validate_sacrifice(self, position, sacrifice_move, see_value):
        """Validate whether a sacrifice has sufficient compensation"""
        
        material_cost = abs(see_value)
        
        # ═══ STEP 1: CLASSIFY SACRIFICE TYPE ═══
        
        sac_type = self.classify_sacrifice(position, sacrifice_move)
        
        if sac_type is None:
            # Unclassifiable → probably just a bad capture
            return SacrificeVerdict(
                should_search=False,
                reason='unclassifiable',
            )
        
        # ═══ STEP 2: IDENTIFY SPECIFIC COMPENSATION ═══
        
        compensation = self.identify_compensation(
            position, sacrifice_move, sac_type
        )
        
        # ═══ STEP 3: QUICK VALIDATION ═══
        
        quick_verdict = self.quick_validate(
            material_cost, compensation, sac_type
        )
        
        if quick_verdict.conclusive:
            return quick_verdict
        
        # ═══ STEP 4: SEARCH-BASED VERIFICATION ═══
        
        verification_depth = self.SACRIFICE_TYPES[sac_type.name][
            'verification_depth'
        ]
        
        return SacrificeVerdict(
            should_search=True,
            search_depth=verification_depth,
            sac_type=sac_type,
            compensation=compensation,
            priority=compensation.total_value / material_cost,
        )
    
    def classify_sacrifice(self, position, move):
        """Classify what TYPE of sacrifice this is"""
        
        opp_king_sq = position.king_square(position.opponent)
        moving_piece = position.piece_at(move.from_square)
        target_sq = move.to_square
        
        # ── King Attack Sacrifice ──
        if (distance(target_sq, opp_king_sq) <= 3 or
            gives_check(position, move)):
            
            # Check if sacrifice opens lines to king
            lines_to_king = count_open_lines_to_king(
                position, move, opp_king_sq
            )
            
            if lines_to_king >= 1 or gives_check(position, move):
                return SacrificeType(
                    name='king_attack',
                    confidence=0.7 + lines_to_king * 0.1,
                )
        
        # ── Deflection Sacrifice ──
        captured_piece = position.piece_at(target_sq)
        if captured_piece:
            # What was captured piece defending?
            defended_targets = find_defended_targets(
                position, captured_piece
            )
            
            for target in defended_targets:
                if (target.value > captured_piece.value or
                    is_mate_square(position, target.square)):
                    return SacrificeType(
                        name='deflection',
                        confidence=0.8,
                        details={'deflected_from': target},
                    )
        
        # ── Clearance Sacrifice ──
        # Does moving piece clear square/line for another piece?
        cleared_lines = find_cleared_lines(position, move)
        if cleared_lines:
            for line in cleared_lines:
                # Is there a piece that benefits from this line?
                beneficiary = find_line_beneficiary(position, line)
                if beneficiary:
                    return SacrificeType(
                        name='clearance',
                        confidence=0.6,
                        details={'cleared_line': line, 
                                 'beneficiary': beneficiary},
                    )
        
        # ── Destruction Sacrifice ──
        if target_sq in get_pawn_shield_squares(position, opp_king_sq):
            return SacrificeType(
                name='destruction',
                confidence=0.7,
            )
        
        # ── Piece Activity Sacrifice ──
        if moving_piece.type in [KNIGHT, BISHOP]:
            sim = position.copy()
            sim.make_move(move)
            
            activity_before = compute_piece_activity(position, position.side_to_move)
            activity_after = compute_piece_activity(sim, position.opponent)
            
            if activity_after > activity_before * 1.3:
                return SacrificeType(
                    name='piece_activity',
                    confidence=0.5,
                )
        
        return None
    
    def identify_compensation(self, position, move, sac_type):
        """Identify specific compensation elements"""
        
        compensation = CompensationProfile()
        
        sim = position.copy()
        sim.make_move(move)
        
        # ── COMPENSATION ELEMENTS ──
        
        # 1. King safety change
        opp = position.opponent
        king_safety_before = evaluate_king_safety(position, opp)
        king_safety_after = evaluate_king_safety(sim, opp)
        king_safety_drop = king_safety_before - king_safety_after
        
        if king_safety_drop > 50:
            compensation.add_element(CompensationElement(
                type='king_safety_drop',
                value=king_safety_drop * 0.7,
                description=f'Opponent king safety dropped by {king_safety_drop}',
            ))
        
        # 2. Piece activity gain
        our_activity_before = compute_piece_activity(position, position.side_to_move)
        our_activity_after = compute_piece_activity(sim, position.side_to_move)
        activity_gain = our_activity_after - our_activity_before
        
        if activity_gain > 30:
            compensation.add_element(CompensationElement(
                type='activity_gain',
                value=activity_gain * 0.5,
                description=f'Piece activity gain: {activity_gain}',
            ))
        
        # 3. Development lead
        dev_lead = compute_development_lead(sim, position.side_to_move)
        if dev_lead > 2:
            compensation.add_element(CompensationElement(
                type='development_lead',
                value=dev_lead * 40,
            ))
        
        # 4. Pawn structure damage inflicted
        pawn_damage = compute_pawn_structure_damage(
            position, sim, opp
        )
        if pawn_damage > 20:
            compensation.add_element(CompensationElement(
                type='pawn_damage',
                value=pawn_damage * 0.6,
            ))
        
        # 5. Control of key squares
        key_squares = get_key_squares(position)
        control_gain = 0
        for sq in key_squares:
            before = square_control(position, sq, position.side_to_move)
            after = square_control(sim, sq, position.side_to_move)
            control_gain += max(0, after - before)
        
        if control_gain > 0:
            compensation.add_element(CompensationElement(
                type='square_control',
                value=control_gain * 25,
            ))
        
        # 6. Passed pawn creation
        new_passers = find_new_passed_pawns(position, sim, position.side_to_move)
        for passer in new_passers:
            rank = relative_rank(passer, position.side_to_move)
            compensation.add_element(CompensationElement(
                type='passed_pawn',
                value=30 * rank,
            ))
        
        # 7. Initiative / Tempo
        if gives_check(position, move):
            compensation.add_element(CompensationElement(
                type='tempo_check',
                value=80,
            ))
        
        forcing_moves_after = count_forcing_moves(sim, position.side_to_move)
        if forcing_moves_after >= 3:
            compensation.add_element(CompensationElement(
                type='initiative',
                value=forcing_moves_after * 25,
            ))
        
        # ═══ COMPUTE TOTAL ═══
        
        compensation.compute_total()
        
        return compensation
    
    def quick_validate(self, material_cost, compensation, sac_type):
        """Quick validation without search"""
        
        ratio = compensation.total_value / max(material_cost, 1)
        
        # Clear cases
        if ratio > 1.5:
            # Compensation clearly exceeds material cost
            return SacrificeVerdict(
                should_search=True,
                conclusive=True,
                search_depth=self.SACRIFICE_TYPES[sac_type.name][
                    'verification_depth'
                ],
                priority=ratio,
                reason='clear_compensation',
            )
        
        if ratio < 0.3:
            # Compensation clearly insufficient
            return SacrificeVerdict(
                should_search=False,
                conclusive=True,
                reason='insufficient_compensation',
            )
        
        # Unclear → need search
        return SacrificeVerdict(conclusive=False)


class CompensationProfile:
    """Profile of compensation elements"""
    
    def __init__(self):
        self.elements = []
        self.total_value = 0
    
    def add_element(self, element):
        self.elements.append(element)
    
    def compute_total(self):
        # Sum with diminishing returns for multiple elements
        values = sorted([e.value for e in self.elements], reverse=True)
        
        total = 0
        for i, val in enumerate(values):
            # Diminishing factor: 1.0, 0.8, 0.6, 0.5, ...
            factor = max(0.4, 1.0 - i * 0.2)
            total += val * factor
        
        self.total_value = total
```

---

## VII. Extension 6: Tactical Pattern Memory

### 7.1 Core Concept

```
Problem: Every QSearch call analyzes from scratch
  Position A: "Is there a fork on c2?" → compute → yes
  Position B (similar): "Is there a fork on c2?" → compute → yes
  
  WASTED: Both positions have same tactical pattern!

Pattern Memory:
  - Extract ABSTRACT tactical patterns from positions
  - Store: pattern → QSearch result mapping
  - On new position: match patterns → reuse results
  
  Example patterns:
  ┌────────────────────────────────────────────────────────────┐
  │ Pattern: "Knight fork on c2 (K on a1, R on e1)"           │
  │ Hash: f(piece_config_around_c2) = 0x3A7F...              │
  │ Stored result: fork wins exchange (net +200cp)            │
  │                                                            │
  │ Pattern: "Greek gift sacrifice (Bxh7+, Ng5, Qh5)"         │
  │ Hash: f(king_position, bishop_diagonal, knight_route)     │
  │ Stored result: sacrifice works if opponent has no Nf6     │
  │                                                            │
  │ Pattern: "Back rank mate threat (R on open file, Kg8)"     │
  │ Hash: f(rook_file, king_position, back_rank_squares)      │
  │ Stored result: mate if no back rank escape                │
  └────────────────────────────────────────────────────────────┘

Key benefit: avoid re-computing KNOWN tactical outcomes
```

### 7.2 Implementation

```python
class TacticalPatternMemory:
    """Store and retrieve tactical patterns for QSearch optimization"""
    
    def __init__(self, max_patterns=100000):
        self.pattern_db = LRUCache(max_size=max_patterns)
        self.pattern_extractor = PatternExtractor()
        self.hit_count = 0
        self.miss_count = 0
    
    def probe(self, position, resolution):
        """Look up position in pattern memory"""
        
        # Extract pattern signature
        patterns = self.pattern_extractor.extract(position)
        
        results = []
        
        for pattern in patterns:
            entry = self.pattern_db.get(pattern.hash)
            
            if entry is not None:
                # Verify pattern still applies
                if self.verify_pattern_match(position, pattern, entry):
                    self.hit_count += 1
                    results.append(PatternMatch(
                        pattern=pattern,
                        entry=entry,
                        confidence=entry.confidence,
                    ))
        
        if not results:
            self.miss_count += 1
            return None
        
        # Return highest-confidence match
        best_match = max(results, key=lambda m: m.confidence)
        
        return best_match
    
    def store(self, position, patterns, qsearch_result):
        """Store QSearch result indexed by patterns"""
        
        for pattern in patterns:
            entry = PatternEntry(
                hash=pattern.hash,
                pattern_type=pattern.type,
                result_score=qsearch_result.score,
                best_move=qsearch_result.best_move,
                confidence=self.compute_confidence(pattern, qsearch_result),
                depth_searched=qsearch_result.depth,
                creation_time=time.time(),
                access_count=0,
            )
            
            self.pattern_db.put(pattern.hash, entry)
    
    def verify_pattern_match(self, position, pattern, entry):
        """Verify that stored pattern still applies to this position"""
        
        # Check that key squares have same configuration
        for sq, expected_piece in pattern.key_squares.items():
            actual_piece = position.piece_at(sq)
            if actual_piece != expected_piece:
                return False
        
        # Check that key lines are still open/blocked
        for line_info in pattern.key_lines:
            if line_info.should_be_open:
                if not is_line_open(position, line_info.from_sq, line_info.to_sq):
                    return False
            else:
                if is_line_open(position, line_info.from_sq, line_info.to_sq):
                    return False
        
        return True
    
    def compute_confidence(self, pattern, result):
        """Compute confidence in pattern result"""
        
        base = 0.5
        
        # Deeper search → more confident
        base += min(result.depth / 10.0, 0.3)
        
        # Clear result (not close to window edge) → more confident
        if abs(result.score) > 50:
            base += 0.1
        
        # Pattern specificity → more confident
        base += len(pattern.key_squares) * 0.02
        
        return min(base, 0.95)


class PatternExtractor:
    """Extract tactical patterns from positions"""
    
    def extract(self, position):
        """Extract all recognizable tactical patterns"""
        
        patterns = []
        
        # ── Pattern Type 1: FORK PATTERNS ──
        
        fork_patterns = self.extract_fork_patterns(position)
        patterns.extend(fork_patterns)
        
        # ── Pattern Type 2: PIN/SKEWER PATTERNS ──
        
        pin_patterns = self.extract_pin_patterns(position)
        patterns.extend(pin_patterns)
        
        # ── Pattern Type 3: DISCOVERED ATTACK PATTERNS ──
        
        discovery_patterns = self.extract_discovery_patterns(position)
        patterns.extend(discovery_patterns)
        
        # ── Pattern Type 4: BACK RANK PATTERNS ──
        
        back_rank_patterns = self.extract_back_rank_patterns(position)
        patterns.extend(back_rank_patterns)
        
        # ── Pattern Type 5: PROMOTION PATTERNS ──
        
        promo_patterns = self.extract_promotion_patterns(position)
        patterns.extend(promo_patterns)
        
        # ── Pattern Type 6: EXCHANGE SEQUENCE PATTERNS ──
        
        exchange_patterns = self.extract_exchange_patterns(position)
        patterns.extend(exchange_patterns)
        
        return patterns
    
    def extract_fork_patterns(self, position):
        """Extract fork patterns"""
        
        patterns = []
        
        for piece in position.pieces(position.side_to_move):
            if piece.type not in [KNIGHT, QUEEN, PAWN]:
                continue
            
            # Find fork opportunities
            for dest in piece.legal_destinations():
                attacks = get_attacks_from(position, dest, piece.type)
                valuable_targets = [
                    sq for sq in attacks
                    if (position.piece_at(sq) and
                        position.piece_at(sq).side != piece.side and
                        position.piece_at(sq).value > 100)
                ]
                
                if len(valuable_targets) >= 2:
                    # Fork pattern found
                    key_squares = {
                        piece.square: piece,
                        dest: None,  # Destination (empty or capture)
                    }
                    for target_sq in valuable_targets:
                        key_squares[target_sq] = position.piece_at(target_sq)
                    
                    pattern = TacticalPattern(
                        type='fork',
                        key_squares=key_squares,
                        key_lines=[],
                        hash=self.compute_pattern_hash(
                            'fork', key_squares, []
                        ),
                        piece_type=piece.type,
                        fork_square=dest,
                        targets=valuable_targets,
                    )
                    patterns.append(pattern)
        
        return patterns
    
    def extract_pin_patterns(self, position):
        """Extract pin/skewer patterns"""
        
        patterns = []
        
        # For each sliding piece
        for piece in position.pieces(position.side_to_move):
            if piece.type not in [BISHOP, ROOK, QUEEN]:
                continue
            
            # Check each ray direction
            for direction in get_ray_directions(piece.type):
                ray = get_ray(piece.square, direction)
                
                first_hit = None
                second_hit = None
                
                for sq in ray:
                    blocker = position.piece_at(sq)
                    if blocker:
                        if first_hit is None:
                            first_hit = (sq, blocker)
                        elif second_hit is None:
                            second_hit = (sq, blocker)
                            break
                        else:
                            break
                
                if first_hit and second_hit:
                    f_sq, f_piece = first_hit
                    s_sq, s_piece = second_hit
                    
                    # Pin: first piece is opponent's, second is more valuable
                    if (f_piece.side != piece.side and 
                        s_piece.side != piece.side and
                        s_piece.value > f_piece.value):
                        
                        key_squares = {
                            piece.square: piece,
                            f_sq: f_piece,
                            s_sq: s_piece,
                        }
                        
                        key_lines = [LineInfo(
                            from_sq=piece.square,
                            to_sq=s_sq,
                            should_be_open=False,
                            blocker=f_sq,
                        )]
                        
                        pattern = TacticalPattern(
                            type='pin',
                            key_squares=key_squares,
                            key_lines=key_lines,
                            hash=self.compute_pattern_hash(
                                'pin', key_squares, key_lines
                            ),
                            pinner=piece,
                            pinned=f_piece,
                            behind=s_piece,
                        )
                        patterns.append(pattern)
        
        return patterns
    
    def extract_back_rank_patterns(self, position):
        """Extract back rank mate patterns"""
        
        patterns = []
        
        for side in [WHITE, BLACK]:
            opp = side ^ 1
            king_sq = position.king_square(opp)
            
            # Is king on back rank?
            back_rank = 0 if opp == WHITE else 7
            if rank_of(king_sq) != back_rank:
                continue
            
            # Are pawns blocking king?
            blocking_pawns = 0
            for adj_file in [file_of(king_sq) - 1, file_of(king_sq), 
                            file_of(king_sq) + 1]:
                if 0 <= adj_file <= 7:
                    pawn_sq = make_square(adj_file, back_rank + (1 if opp == WHITE else -1))
                    piece = position.piece_at(pawn_sq)
                    if piece and piece.type == PAWN and piece.side == opp:
                        blocking_pawns += 1
            
            if blocking_pawns >= 2:
                # Back rank weakness detected
                
                # Do we have a rook/queen that can exploit?
                back_rank_file = get_open_file_to_back_rank(
                    position, side, back_rank
                )
                
                if back_rank_file is not None:
                    key_squares = {king_sq: position.piece_at(king_sq)}
                    
                    pattern = TacticalPattern(
                        type='back_rank',
                        key_squares=key_squares,
                        key_lines=[],
                        hash=self.compute_pattern_hash(
                            'back_rank', key_squares, []
                        ),
                        attacking_side=side,
                        target_rank=back_rank,
                    )
                    patterns.append(pattern)
        
        return patterns
    
    def compute_pattern_hash(self, pattern_type, key_squares, key_lines):
        """Compute hash for pattern (position-independent)"""
        
        h = hash(pattern_type)
        
        for sq, piece in sorted(key_squares.items()):
            h ^= zobrist_piece(piece, sq) if piece else zobrist_empty(sq)
        
        for line in key_lines:
            h ^= hash((line.from_sq, line.to_sq, line.should_be_open))
        
        return h
```

---

## VIII. Extension 7: Boundary Smoothing

### 8.1 The Problem

```
Current transition: ABRUPT

  Main search depth 2: generate ALL moves, full search
  Main search depth 1: generate ALL moves, full search
  Main search depth 0: → QSearch (SUDDENLY captures only!)
  
  This causes:
  1. Important quiet moves at depth 0 are NEVER seen
  2. QSearch can't benefit from main search information
  3. Move generation changes discontinuously
  
  Better: GRADIENT transition over 3-4 plies
  
  ┌────────────────────────────────────────────────────────────┐
  │ Depth 3: Full search (all moves, full depth)              │
  │ Depth 2: Extended search (all moves, slight reductions)   │
  │ Depth 1: TRANSITION ZONE - all captures + critical quiets │
  │ Depth 0: TRANSITION ZONE - good captures + key quiets     │
  │ Depth -1: DQRS - captures + conditional quiets            │
  │ Depth -2: DQRS - good captures only                       │
  │ Depth -3: DQRS - critical captures only                   │
  └────────────────────────────────────────────────────────────┘
```

### 8.2 Implementation

```python
class BoundarySmoothing:
    """Smooth transition between main search and QSearch"""
    
    # Transition zone: depth 2 to depth -2
    TRANSITION_START = 2   # Start smoothing at this depth
    TRANSITION_END = -2    # Fully in QSearch at this depth
    
    def get_move_generation_config(self, depth, resolution, is_pv):
        """Get move generation configuration for given depth"""
        
        if depth > self.TRANSITION_START:
            # Full main search
            return MoveGenConfig(
                generate_all_moves=True,
                generate_captures=True,
                generate_quiet_checks=True,
                generate_quiet_moves=True,
                max_quiet_moves=MAX_MOVES,
                reductions_enabled=True,
            )
        
        elif depth > 0:
            # TRANSITION ZONE (depth 1-2)
            
            # How deep in transition? 0.0 = just entered, 1.0 = about to QSearch
            transition_progress = 1.0 - (depth / self.TRANSITION_START)
            
            # Number of quiet moves decreases through transition
            max_quiets = int(MAX_MOVES * (1.0 - transition_progress * 0.7))
            
            # Only "important" quiet moves as we approach QSearch
            quiet_importance_threshold = transition_progress * 0.5
            
            return MoveGenConfig(
                generate_all_moves=False,
                generate_captures=True,
                generate_quiet_checks=True,
                generate_quiet_moves=True,
                max_quiet_moves=max_quiets,
                quiet_importance_threshold=quiet_importance_threshold,
                reductions_enabled=True,
                extra_reduction=transition_progress * 0.5,
            )
        
        elif depth == 0:
            # BOUNDARY POINT
            # Use resolution to decide behavior
            
            if resolution.score < 0.5:
                # Unresolved → enter QSearch with broad move generation
                return MoveGenConfig(
                    generate_all_moves=False,
                    generate_captures=True,
                    generate_quiet_checks=True,
                    generate_quiet_moves=True,
                    max_quiet_moves=10,
                    quiet_importance_threshold=0.6,
                    reductions_enabled=True,
                    extra_reduction=1.0,
                )
            else:
                # Mostly resolved → narrow QSearch
                return MoveGenConfig(
                    generate_all_moves=False,
                    generate_captures=True,
                    generate_quiet_checks=(resolution.quiet_profile.king_quiet < 0.5),
                    generate_quiet_moves=False,
                    max_quiet_moves=0,
                )
        
        else:
            # depth < 0 → Full DQRS territory
            return MoveGenConfig(
                use_dqrs_tiered_generation=True,
                resolution=resolution,
            )
    
    def get_search_function(self, depth, resolution):
        """Get which search function to use at this depth"""
        
        if depth > self.TRANSITION_START:
            return 'main_search'
        
        elif depth > 0:
            return 'transition_search'
        
        else:
            return 'dqrs'
    
    def transition_search(self, position, alpha, beta, depth, 
                           resolution):
        """Search in transition zone"""
        
        config = self.get_move_generation_config(depth, resolution, False)
        
        stand_pat = nnue_eval(position)
        
        # In transition zone, use hybrid of main search and QSearch
        
        # Stand pat is available but weighted by transition progress
        transition_progress = 1.0 - (depth / self.TRANSITION_START)
        
        # As we approach QSearch, stand pat becomes more relevant
        if stand_pat >= beta and transition_progress > 0.5:
            # Allow stand-pat cutoff in later transition
            return beta
        
        best_score = -INFINITY
        
        # Generate moves based on config
        moves = generate_moves_with_config(position, config)
        
        for i, move in enumerate(moves):
            # Compute reduction based on transition progress
            reduction = 0
            
            if not move.is_capture and not move.gives_check:
                # Quiet moves get EXTRA reduction in transition zone
                base_lmr = compute_lmr_reduction(depth, i)
                extra = config.extra_reduction
                reduction = base_lmr + extra
            
            new_depth = depth - 1 - reduction
            
            position.make_move(move)
            
            if new_depth <= 0:
                # Transition to DQRS
                score = -dqrs(position, -beta, -alpha, 
                             abs(new_depth) + 2)
            else:
                # Still in transition/main search
                score = -self.transition_search(
                    position, -beta, -alpha, new_depth, resolution
                )
            
            position.unmake_move(move)
            
            if score > best_score:
                best_score = score
            if score > alpha:
                alpha = score
            if score >= beta:
                return beta
        
        if best_score == -INFINITY:
            # No legal moves
            if position.is_in_check():
                return -MATE_VALUE + position.ply
            else:
                return 0  # Stalemate
        
        return best_score
```

---

## IX. Extension 8: QSearch Transposition Optimization

### 9.1 Enhanced QSearch TT

```python
class QSearchTranspositionTable:
    """Enhanced TT specifically for QSearch positions
    
    Key innovations over standard TT in QSearch:
    1. Store RESOLUTION SCORE alongside eval
    2. Store THREAT SIGNATURE for fast matching
    3. Store EXCHANGE SEQUENCE RESULT for reuse
    4. Smaller entries (QSearch doesn't need full TT entry)
    """
    
    class QSearchTTEntry:
        __slots__ = [
            'hash_key',          # 32-bit (vs 64-bit in main TT)
            'score',             # 16-bit
            'resolution_score',  # 8-bit (0-255 mapped to 0.0-1.0)
            'best_move',         # 16-bit
            'depth',             # 8-bit
            'bound_type',        # 2-bit
            'threat_signature',  # 16-bit
            'generation',        # 8-bit
        ]
        # Total: 13 bytes vs 16+ bytes in main TT
    
    def __init__(self, size_mb=64):
        self.num_entries = (size_mb * 1024 * 1024) // 13
        self.table = [None] * self.num_entries
        self.current_generation = 0
    
    def probe(self, position_hash, alpha, beta, depth):
        """Probe QSearch TT"""
        
        index = position_hash % self.num_entries
        entry = self.table[index]
        
        if entry is None:
            return None
        
        # Verify hash match (32-bit → higher collision rate)
        if entry.hash_key != (position_hash >> 32):
            return None
        
        # Depth check (QSearch depth is negative)
        if entry.depth < depth:
            return None  # Need deeper search
        
        # Score bounds
        if entry.bound_type == EXACT:
            return QSProbeResult(
                score=entry.score,
                resolution=entry.resolution_score / 255.0,
                best_move=entry.best_move,
                hit_type='exact',
            )
        
        elif entry.bound_type == LOWER_BOUND and entry.score >= beta:
            return QSProbeResult(
                score=entry.score,
                hit_type='beta_cutoff',
            )
        
        elif entry.bound_type == UPPER_BOUND and entry.score <= alpha:
            return QSProbeResult(
                score=entry.score,
                hit_type='alpha_bound',
            )
        
        # No cutoff, but return useful info
        return QSProbeResult(
            score=None,  # Can't use score for cutoff
            resolution=entry.resolution_score / 255.0,
            best_move=entry.best_move,
            hit_type='info_only',
            threat_signature=entry.threat_signature,
        )
    
    def store(self, position_hash, score, resolution, best_move, 
              depth, bound_type, threat_signature):
        """Store in QSearch TT"""
        
        index = position_hash % self.num_entries
        existing = self.table[index]
        
        # Replacement policy
        should_replace = (
            existing is None or
            existing.generation != self.current_generation or
            depth >= existing.depth or
            bound_type == EXACT
        )
        
        if should_replace:
            entry = self.QSearchTTEntry()
            entry.hash_key = position_hash >> 32
            entry.score = score
            entry.resolution_score = int(resolution * 255)
            entry.best_move = best_move
            entry.depth = depth
            entry.bound_type = bound_type
            entry.threat_signature = threat_signature
            entry.generation = self.current_generation
            
            self.table[index] = entry
    
    def compute_threat_signature(self, threat_inventory):
        """Compact signature of threat state"""
        
        sig = 0
        
        for threat in threat_inventory.immediate[:8]:
            # Hash each threat into 2 bits
            threat_hash = hash((threat.type, threat.severity > 200)) & 0x3
            sig = (sig << 2) | threat_hash
        
        return sig & 0xFFFF


class QSearchTTIntegration:
    """Integrate QSearch TT into DQRS"""
    
    def __init__(self, qs_tt):
        self.qs_tt = qs_tt
    
    def probe_and_use(self, position, alpha, beta, depth, resolution):
        """Probe TT and use results optimally"""
        
        result = self.qs_tt.probe(position.hash_key, alpha, beta, depth)
        
        if result is None:
            return None
        
        if result.hit_type in ['exact', 'beta_cutoff', 'alpha_bound']:
            # Direct score use
            return result
        
        if result.hit_type == 'info_only':
            # Can't use score, but:
            
            # 1. Use best move for move ordering
            # (Put TT move first in tiered generation)
            
            # 2. Use resolution score to skip re-assessment
            if result.resolution is not None and result.resolution > 0.8:
                # Position was resolved in previous search
                # Skip expensive resolution assessment
                pass
            
            # 3. Use threat signature for quick matching
            current_sig = self.qs_tt.compute_threat_signature(
                resolution.threat_inventory
            )
            if current_sig == result.threat_signature:
                # Same threat state → result likely still valid
                # Can boost confidence in TT information
                pass
            
            return result
        
        return None
```

---

## X. Extension 9: Defensive Resource Counting

### 10.1 Core Concept

```
When evaluating a position in QSearch, knowing HOW MANY defensive
resources exist is crucial:

  Position A: 
    Threats against us: Queen battery on g7
    Defensive resources: 3 (Rf8 blocks, Qg6 interposes, Kh8 escapes)
    → Multiple defenses exist → less urgent, moderate QSearch depth
  
  Position B:
    Threats against us: Same Queen battery on g7  
    Defensive resources: 1 (only Kh8 escapes, but then Qh7#)
    → Single defense that doesn't work → CRITICAL, deep QSearch needed
  
  Position C:
    Threats against us: Knight fork on c2
    Defensive resources: 0
    → No defense → threat will materialize → don't search deeply,
      just accept the loss and factor it in
  
  Counting resources tells us:
  - 0 resources: threat will succeed → accept and evaluate
  - 1 resource: MUST verify this resource works → search it
  - 2+ resources: at least one likely works → moderate search
  - Many resources: position is fine → minimal QSearch
```

### 10.2 Implementation

```python
class DefensiveResourceCounter:
    """Count and classify defensive resources against threats"""
    
    def count_resources(self, position, threat, resolution):
        """Count defensive resources against specific threat"""
        
        resources = DefensiveResources()
        
        # ═══ RESOURCE TYPE 1: DIRECT CAPTURE ═══
        # Capture the threatening piece
        
        if threat.source:
            our_attackers = position.attackers_of(
                threat.source.square, position.side_to_move
            )
            
            for attacker in our_attackers:
                see_val = see_from_square(
                    position, attacker, threat.source.square
                )
                
                if see_val >= -50:
                    resources.add(DefensiveResource(
                        type='capture_threat_source',
                        move=Move(attacker, threat.source.square),
                        quality=0.9 if see_val >= 0 else 0.5,
                        material_cost=max(0, -see_val),
                    ))
        
        # ═══ RESOURCE TYPE 2: INTERPOSITION ═══
        # Block the attack line
        
        if threat.is_line_attack():
            blocking_squares = squares_between(
                threat.source.square, threat.target.square
            )
            
            for block_sq in blocking_squares:
                blockers = position.pieces_that_can_reach(
                    block_sq, position.side_to_move
                )
                
                for blocker in blockers:
                    # Don't block with the piece being attacked!
                    if blocker.square == threat.target.square:
                        continue
                    
                    quality = 0.7
                    
                    # Better if blocker is less valuable
                    if blocker.value < threat.target.value:
                        quality += 0.1
                    
                    # Worse if blocker was doing something important
                    if is_piece_active(position, blocker):
                        quality -= 0.1
                    
                    resources.add(DefensiveResource(
                        type='interposition',
                        move=Move(blocker, block_sq),
                        quality=quality,
                    ))
        
        # ═══ RESOURCE TYPE 3: EVASION ═══
        # Move the threatened piece to safety
        
        if threat.target:
            target = threat.target
            safe_squares = find_safe_squares(position, target)
            
            for safe_sq in safe_squares:
                quality = 0.6  # Evasion is passive
                
                # Better if going to active square
                if is_active_square(safe_sq, target.type):
                    quality += 0.2
                
                # Better if creating counter-threat
                creates_threat = creates_counter_threat(
                    position, Move(target, safe_sq)
                )
                if creates_threat:
                    quality += 0.2
                
                resources.add(DefensiveResource(
                    type='evasion',
                    move=Move(target, safe_sq),
                    quality=quality,
                ))
        
        # ═══ RESOURCE TYPE 4: COUNTER-THREAT ═══
        # Play a bigger threat instead
        
        counter_threats = find_counter_threats_to(
            position, threat
        )
        
        for ct in counter_threats:
            if ct.threat_value > threat.severity:
                resources.add(DefensiveResource(
                    type='counter_threat',
                    move=ct.move,
                    quality=0.8,
                    creates_threat=ct.threat_value,
                ))
        
        # ═══ RESOURCE TYPE 5: PROPHYLAXIS ═══
        # Prevent the threat from being carried out
        
        if threat.type == THREAT_FORK and threat.details:
            fork_sq = threat.details.get('fork_square')
            if fork_sq:
                # Control the fork square
                controllers = position.pieces_that_can_reach(
                    fork_sq, position.side_to_move
                )
                for controller in controllers:
                    resources.add(DefensiveResource(
                        type='prophylaxis',
                        move=Move(controller, fork_sq),
                        quality=0.7,
                    ))
        
        # ═══ AGGREGATE ═══
        
        resources.sort_by_quality()
        resources.compute_stats()
        
        return resources
    
    def assess_defense_adequacy(self, position, threats, resolution):
        """Assess overall defensive adequacy"""
        
        adequacy = DefenseAdequacy()
        
        for threat in threats.immediate:
            resources = self.count_resources(
                position, threat, resolution
            )
            
            adequacy.per_threat[threat] = resources
            
            if resources.count == 0:
                adequacy.undefended_threats.append(threat)
            elif resources.count == 1:
                adequacy.single_defense_threats.append(threat)
            else:
                adequacy.adequately_defended_threats.append(threat)
        
        # Overall score
        if adequacy.undefended_threats:
            # At least one undefended threat → very bad
            adequacy.overall_score = 0.1
            adequacy.recommended_action = 'accept_loss'
            
            # Calculate material loss from undefended threats
            adequacy.expected_loss = sum(
                t.severity for t in adequacy.undefended_threats
            )
        
        elif adequacy.single_defense_threats:
            # Single-defense threats → need verification
            adequacy.overall_score = 0.4
            adequacy.recommended_action = 'verify_defense'
            adequacy.verification_moves = [
                adequacy.per_threat[t].best_resource().move
                for t in adequacy.single_defense_threats
            ]
        
        else:
            # All threats adequately defended
            adequacy.overall_score = 0.8 + min(
                0.2, 
                sum(r.count for r in adequacy.per_threat.values()) * 0.02
            )
            adequacy.recommended_action = 'normal_search'
        
        return adequacy


class DefenseAdequacyIntegration:
    """Integrate defense counting into DQRS"""
    
    def adjust_dqrs_behavior(self, position, alpha, beta, 
                               depth_budget, adequacy):
        """Adjust DQRS behavior based on defense adequacy"""
        
        if adequacy.recommended_action == 'accept_loss':
            # Undefended threats → adjust stand pat to account for loss
            adjusted_eval = nnue_eval(position) - adequacy.expected_loss
            
            if adjusted_eval >= beta:
                return beta  # Still good despite loss
            
            # Don't search deeper trying to find defense that doesn't exist
            # Just return adjusted eval
            if depth_budget <= 2:
                return adjusted_eval
            
            # Continue with reduced depth (just verify)
            return dqrs(position, alpha, beta, 
                       max(2, depth_budget - 3))
        
        elif adequacy.recommended_action == 'verify_defense':
            # Search verification moves first
            best = -INFINITY
            
            for defense_move in adequacy.verification_moves:
                position.make_move(defense_move)
                score = -dqrs(position, -beta, -alpha, 
                             depth_budget)  # Full depth for verification
                position.unmake_move(defense_move)
                
                best = max(best, score)
                if score > alpha:
                    alpha = score
                if score >= beta:
                    return beta
            
            return best
        
        else:
            # Normal search
            return None  # Let DQRS handle normally
```

---

## XI. Unified DQRS-X Architecture

### 11.1 Complete Integration Flow

```
┌──────────────────────────────────────────────────────────────────────────┐
│                      DQRS-X COMPLETE ARCHITECTURE                        │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                    ENTRY & BOUNDARY (Ext 7)                      │   │
│  │                                                                  │   │
│  │  Main Search depth 2-0: Boundary Smoothing                       │   │
│  │  Gradual transition: all moves → captures + critical quiets      │   │
│  │  Resolution-aware move generation config                        │   │
│  └─────────────────────────────┬────────────────────────────────────┘   │
│                                ▼                                         │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │              RESOLUTION ASSESSMENT (Enhanced)                    │   │
│  │                                                                  │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐          │   │
│  │  │Tactical  │ │ Threat   │ │ Threat   │ │Defensive │          │   │
│  │  │Tension   │ │Inventory │ │ Chain    │ │ Resource │          │   │
│  │  │Analyzer  │ │          │ │ DAG(E2)  │ │Count(E9) │          │   │
│  │  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘          │   │
│  │       └─────┬───────┴─────┬──────┴─────┬──────┘               │   │
│  │             ▼             ▼            ▼                        │   │
│  │      ┌──────────────────────────────────────┐                  │   │
│  │      │ Pattern Memory Probe (Ext 6)         │                  │   │
│  │      └──────────────┬───────────────────────┘                  │   │
│  │                     ▼                                           │   │
│  │      ┌──────────────────────────────────────┐                  │   │
│  │      │ Resolution Score + Defense Adequacy   │                  │   │
│  │      └──────────────┬───────────────────────┘                  │   │
│  └─────────────────────┼────────────────────────────────────────────┘   │
│                        ▼                                                 │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │              STAND PAT & EARLY DECISIONS                         │   │
│  │                                                                  │   │
│  │  ┌────────────────────┐  ┌──────────────────────────────────┐  │   │
│  │  │ Threat-Adjusted    │  │ Sacrifice Quick Validation (E5)  │  │   │
│  │  │ Stand Pat          │  │                                  │  │   │
│  │  └────────────────────┘  └──────────────────────────────────┘  │   │
│  │                                                                  │   │
│  │  ┌────────────────────┐  ┌──────────────────────────────────┐  │   │
│  │  │ QSearch TT Probe   │  │ Eval Trajectory Check (Ext 4)   │  │   │
│  │  │ (Ext 8)            │  │                                  │  │   │
│  │  └────────────────────┘  └──────────────────────────────────┘  │   │
│  └─────────────────────────┬────────────────────────────────────────┘   │
│                            ▼                                             │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │              DEPTH & MOVE ALLOCATION                             │   │
│  │                                                                  │   │
│  │  ┌──────────────────────┐  ┌──────────────────────────────┐    │   │
│  │  │ Asymmetric Depth     │  │ Tiered Move Generation       │    │   │
│  │  │ Allocation (Ext 3)   │  │ (with ESA for exchanges, E1) │    │   │
│  │  └──────────────────────┘  └──────────────────────────────┘    │   │
│  └─────────────────────────┬────────────────────────────────────────┘   │
│                            ▼                                             │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │              SEARCH LOOP                                         │   │
│  │                                                                  │   │
│  │  for each move:                                                  │   │
│  │    ├─ Exchange? → ESA optimized search (Ext 1)                  │   │
│  │    ├─ Sacrifice? → SVF validation (Ext 5)                       │   │
│  │    ├─ Normal capture → standard DQRS                            │   │
│  │    ├─ Critical quiet → depth from chain priority (Ext 2)        │   │
│  │    │                                                             │   │
│  │    ├─ Record eval for trajectory (Ext 4)                        │   │
│  │    ├─ Check convergence                                         │   │
│  │    └─ Check explosion budget                                    │   │
│  └─────────────────────────┬────────────────────────────────────────┘   │
│                            ▼                                             │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │              POST-SEARCH                                         │   │
│  │                                                                  │   │
│  │  ┌──────────────────────┐  ┌──────────────────────────────┐    │   │
│  │  │ Store to QSearch TT  │  │ Store to Pattern Memory      │    │   │
│  │  │ (Ext 8)              │  │ (Ext 6)                      │    │   │
│  │  └──────────────────────┘  └──────────────────────────────┘    │   │
│  │                                                                  │   │
│  │  ┌──────────────────────────────────────────────────────────┐  │   │
│  │  │ Update Trajectory Predictor (Ext 4)                      │  │   │
│  │  └──────────────────────────────────────────────────────────┘  │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────────┘
```

### 11.2 Main Loop — DQRS-X Complete

```python
def dqrs_x(position, alpha, beta, depth_budget, 
           main_search_context=None, parent_trajectory=None):
    """Complete DQRS-X: Extended Deep Quiet Resolution Search"""
    
    # ═══════════════════════════════════════════
    # PHASE 0: TT PROBE
    # ═══════════════════════════════════════════
    
    qs_tt_result = qs_tt.probe(position.hash_key, alpha, beta, depth_budget)
    
    if qs_tt_result and qs_tt_result.score is not None:
        return qs_tt_result.score  # TT cutoff
    
    tt_best_move = qs_tt_result.best_move if qs_tt_result else None
    tt_resolution = qs_tt_result.resolution if qs_tt_result else None
    
    # ═══════════════════════════════════════════
    # PHASE 1: RESOLUTION ASSESSMENT
    # ═══════════════════════════════════════════
    
    # [Ext 6] Pattern memory probe first (cheapest)
    pattern_match = pattern_memory.probe(position, None)
    
    if pattern_match and pattern_match.confidence > 0.85:
        # High-confidence pattern match → use stored result
        if alpha < pattern_match.entry.result_score < beta:
            return pattern_match.entry.result_score
    
    # Use TT resolution if available (avoid recomputation)
    if tt_resolution is not None and tt_resolution > 0.8:
        # TT says position was resolved → fast path
        resolution = FastResolution(score=tt_resolution)
    else:
        # Full resolution assessment
        tension = tension_analyzer.compute_tension(position)
        threats = threat_inventory.catalog_threats(position)
        
        # [Ext 2] Build threat chain DAG
        threat_dag = threat_chain_dag.build_dag(position, threats)
        
        # [Ext 9] Count defensive resources
        defense = resource_counter.assess_defense_adequacy(
            position, threats, None
        )
        
        quiet = classifier.classify(position, tension, threats)
        
        resolution = ResolutionResult(
            score=quiet.resolution_score,
            tension_profile=tension,
            threat_inventory=threats,
            quiet_profile=quiet,
            threat_dag=threat_dag,
            defense_adequacy=defense,
        )
        
        # [Ext 2] Adjust resolution based on threat chains
        resolution.score = threat_chain_integration.adjust_resolution_score(
            resolution.score, threat_dag
        )
    
    # ═══════════════════════════════════════════
    # PHASE 2: STAND PAT & EARLY DECISIONS
    # ═══════════════════════════════════════════
    
    # [DQRS Base] Threat-adjusted stand pat
    stand_pat_result = threat_adjusted_stand_pat(position, resolution)
    adjusted_eval = stand_pat_result.adjusted_eval
    
    # [Ext 9] Defense adequacy override
    if hasattr(resolution, 'defense_adequacy'):
        adequacy = resolution.defense_adequacy
        
        defense_result = defense_integration.adjust_dqrs_behavior(
            position, alpha, beta, depth_budget, adequacy
        )
        
        if defense_result is not None:
            return defense_result  # Defense-directed search handled it
    
    # Beta cutoff
    if adjusted_eval >= beta:
        qs_tt.store(position.hash_key, adjusted_eval, 
                    resolution.score, None, depth_budget, LOWER_BOUND,
                    qs_tt.compute_threat_signature(resolution.threat_inventory))
        return beta
    
    if adjusted_eval > alpha:
        alpha = adjusted_eval
    
    # [Ext 4] Trajectory prediction
    trajectory = EvalTrajectoryPredictor()
    trajectory.record_eval(depth_budget, adjusted_eval, 'stand_pat')
    
    if parent_trajectory:
        # Inherit trajectory from parent for better prediction
        trajectory.inherit(parent_trajectory)
    
    # Check if trajectory predicts convergence
    if trajectory.should_stop_search(depth_budget, alpha, beta):
        prediction = trajectory.predict_convergence(depth_budget)
        return clamp(prediction.converge_value, alpha, beta)
    
    # Early termination checks
    termination = check_termination(
        position, resolution, depth_budget, alpha, beta, adjusted_eval
    )
    if termination.should_stop:
        return termination.score
    
    # ═══════════════════════════════════════════
    # PHASE 3: DEPTH & MOVE ALLOCATION
    # ═══════════════════════════════════════════
    
    # [Ext 3] Asymmetric depth allocation
    allocation = asymmetric_allocator.compute_allocation(
        position, resolution, depth_budget
    )
    
    effective_depth = allocation.stm_depth
    
    # [DQRS Base] Tiered move generation
    move_gen = TieredMoveGenerator(
        position, resolution,
        SearchState(tt_move=tt_best_move)
    )
    
    # [Ext 2] Reorder moves by threat chain priority
    if resolution.threat_dag and resolution.threat_dag.nodes:
        threat_chain_integration.prioritize_moves_by_chain(
            move_gen, resolution.threat_dag, position
        )
    
    # ═══════════════════════════════════════════
    # PHASE 4: SEARCH LOOP
    # ═══════════════════════════════════════════
    
    best_score = adjusted_eval
    best_move = None
    moves_searched = 0
    
    while True:
        move, tier_name = move_gen.next_move()
        
        if move is None:
            break
        
        moves_searched += 1
        
        # Budget check
        if explosion_prevention.check_budget():
            if tier_name not in ['TT_MOVE', 'CRITICAL_CAPTURES']:
                break
        
        # Per-move pruning
        prune = dqrs_move_pruning(
            position, move, tier_name, resolution,
            alpha, beta, adjusted_eval, effective_depth, moves_searched
        )
        
        if prune.action == SKIP:
            continue
        
        # ═══ SPECIAL HANDLING BY MOVE TYPE ═══
        
        # [Ext 1] Exchange sequence detection
        if move.is_capture and is_exchange_sequence_start(position, move):
            esa_sequence = esa.analyze_exchange(position, move)
            
            if esa_sequence.insertion_points or esa_sequence.possible_deviations:
                # Complex exchange → use ESA-optimized search
                position.make_move(move)
                score = -exchange_search.search_exchange(
                    position, esa_sequence, -beta, -alpha
                )
                position.unmake_move(move)
                
                # Record trajectory
                trajectory.record_eval(effective_depth - 1, -score, 'exchange')
                
                if score > best_score:
                    best_score = score
                    best_move = move
                if score > alpha:
                    alpha = score
                if score >= beta:
                    break
                
                continue  # Handled by ESA, skip normal search
        
        # [Ext 5] Sacrifice detection
        if move.is_capture:
            see_val = see(position, move)
            
            if see_val < -100:
                # Potential sacrifice → validate
                verdict = sacrifice_validator.validate_sacrifice(
                    position, move, see_val
                )
                
                if not verdict.should_search:
                    continue  # Skip — insufficient compensation
                
                # Search at specified depth
                child_depth = min(effective_depth - 1, verdict.search_depth)
                
                position.make_move(move)
                score = -dqrs_x(
                    position, -beta, -alpha, child_depth,
                    parent_trajectory=trajectory,
                )
                position.unmake_move(move)
                
                trajectory.record_eval(child_depth, -score, 'sacrifice')
                
                if score > best_score:
                    best_score = score
                    best_move = move
                if score > alpha:
                    alpha = score
                if score >= beta:
                    break
                
                continue  # Handled by SVF
        
        # ═══ NORMAL SEARCH PATH ═══
        
        child_depth = compute_child_depth(
            move, tier_name, prune, effective_depth, resolution
        )
        
        # [Ext 3] Apply asymmetric depth for opponent's turn
        # (After our move, it's opponent's turn → use opp_depth)
        if child_depth > 0:
            child_depth = min(child_depth, allocation.opp_depth)
        
        position.make_move(move)
        
        if child_depth <= 0 and not position.is_in_check():
            # Leaf: threat-adjusted eval
            child_resolution = compute_resolution_incremental(
                position, resolution, move
            )
            leaf_eval = threat_adjusted_stand_pat(
                position, child_resolution
            ).adjusted_eval
            score = -leaf_eval
        else:
            score = -dqrs_x(
                position, -beta, -alpha, child_depth,
                parent_trajectory=trajectory,
            )
        
        position.unmake_move(move)
        
        # Record trajectory
        move_type = 'capture' if move.is_capture else 'quiet'
        trajectory.record_eval(child_depth, -score, move_type)
        
        # Update best
        if score > best_score:
            best_score = score
            best_move = move
        if score > alpha:
            alpha = score
        if score >= beta:
            break
        
        # [Ext 4] Check trajectory convergence mid-search
        if moves_searched >= 3 and trajectory.should_stop_search(
            effective_depth, alpha, beta
        ):
            break
        
        # [Ext 4] Check for divergence (tactical storm)
        if trajectory.detect_divergence(effective_depth):
            # Tactical storm → increase depth budget
            effective_depth = min(effective_depth + 2, depth_budget + 4)
        
        # Move count limits per tier
        if moves_searched >= max_moves_for_tier(tier_name, resolution):
            if tier_name not in ['TT_MOVE', 'CRITICAL_CAPTURES']:
                move_gen.advance_to_next_tier()
    
    # ═══════════════════════════════════════════
    # PHASE 5: POST-SEARCH
    # ═══════════════════════════════════════════
    
    # Determine bound type
    if best_score >= beta:
        bound_type = LOWER_BOUND
    elif best_score > adjusted_eval:
        bound_type = EXACT
    else:
        bound_type = UPPER_BOUND
    
    # [Ext 8] Store to QSearch TT
    qs_tt.store(
        position.hash_key, best_score, resolution.score,
        best_move, depth_budget, bound_type,
        qs_tt.compute_threat_signature(
            resolution.threat_inventory if hasattr(resolution, 'threat_inventory') else None
        ),
    )
    
    # [Ext 6] Store to pattern memory
    if hasattr(resolution, 'tension_profile'):
        patterns = pattern_extractor.extract(position)
        if patterns:
            pattern_memory.store(
                position, patterns,
                QSearchResult(
                    score=best_score,
                    best_move=best_move,
                    depth=depth_budget,
                ),
            )
    
    return best_score
```

---

## XII. Ước Tính Ảnh Hưởng Tổng Hợp

```
┌──────────────────────────────────────────────┬────────────┬──────────────┐
│ Extension                                    │ Elo Est.   │ Confidence   │
├──────────────────────────────────────────────┼────────────┼──────────────┤
│ DQRS Base (from original doc)                │ +30-55     │ ★★★★ High    │
│                                              │            │              │
│ Ext 1: Exchange Sequence Algebra             │ +12-25     │ ★★★★ High    │
│   - Structured exchange modeling             │ +5-10      │              │
│   - Zwischenzug detection in exchanges       │ +5-10      │              │
│   - Deviation-only search (node savings)     │ +3-8       │              │
│                                              │            │              │
│ Ext 2: Threat Chain DAG                      │ +8-18      │ ★★★ Medium   │
│   - Cascading threat prediction              │ +3-8       │              │
│   - Chain-based move prioritization          │ +3-6       │              │
│   - Resolution adjustment from chains        │ +2-5       │              │
│                                              │            │              │
│ Ext 3: Asymmetric Depth Allocation           │ +8-15      │ ★★★★ High    │
│   - Initiative-aware depth splitting         │ +5-8       │              │
│   - Node savings from reduced opponent depth │ +3-7       │              │
│                                              │            │              │
│ Ext 4: Eval Trajectory Prediction            │ +10-22     │ ★★★★ High    │
│   - Convergence-based early stopping         │ +5-12      │              │
│   - Adaptive delta pruning margin            │ +3-6       │              │
│   - Divergence detection (storm warning)     │ +2-5       │              │
│                                              │            │              │
│ Ext 5: Sacrifice Validation Framework        │ +10-25     │ ★★★ Medium   │
│   - Sacrifice classification                 │ +3-8       │              │
│   - Compensation identification              │ +4-10      │              │
│   - Targeted verification search             │ +3-8       │              │
│                                              │            │              │
│ Ext 6: Tactical Pattern Memory               │ +6-15      │ ★★★ Medium   │
│   - Pattern extraction & matching            │ +3-8       │              │
│   - Cross-position result reuse              │ +3-7       │              │
│                                              │            │              │
│ Ext 7: Boundary Smoothing                    │ +5-12      │ ★★★★ High    │
│   - Gradual transition zone                  │ +3-7       │              │
│   - Resolution-aware boundary behavior       │ +2-5       │              │
│                                              │            │              │
│ Ext 8: QSearch TT Optimization               │ +5-12      │ ★★★★ High    │
│   - Compact QSearch-specific TT              │ +3-6       │              │
│   - Resolution & threat signature storage    │ +2-6       │              │
│                                              │            │              │
│ Ext 9: Defensive Resource Counting           │ +6-14      │ ★★★ Medium   │
│   - Resource-aware QSearch depth             │ +3-7       │              │
│   - Undefended threat acceptance             │ +2-4       │              │
│   - Verification-focused search              │ +2-4       │              │
├──────────────────────────────────────────────┼────────────┼──────────────┤
│ Extensions Total (with overlap & interaction)│ +45-95     │              │
│ DQRS Base + All Extensions                   │ +65-130    │              │
│ After overhead deduction (-15-25%)           │ +50-105    │              │
│ Conservative estimate                        │ +40-75     │              │
│ Realistic center estimate                    │ +55-85     │              │
└──────────────────────────────────────────────┴────────────┴──────────────┘

By position type:
┌──────────────────────────────┬────────────┬──────────────────────────────┐
│ Position Type                │ Improvement│ Dominant Extensions          │
├──────────────────────────────┼────────────┼──────────────────────────────┤
│ Exchange-heavy middlegame    │ +50-90 Elo │ ESA(E1), Trajectory(E4),    │
│                              │            │ QS-TT(E8)                   │
│ Tactical / Combinations      │ +60-110 Elo│ SVF(E5), ThreatDAG(E2),     │
│                              │            │ Pattern(E6), Defense(E9)    │
│ King attack / Sacrifice      │ +55-100 Elo│ SVF(E5), ThreatDAG(E2),     │
│                              │            │ Asymmetric(E3)              │
│ Quiet positional             │ +15-30 Elo │ Boundary(E7), QS-TT(E8),    │
│                              │            │ Trajectory(E4)              │
│ Endgame (few pieces)         │ +30-55 Elo │ Defense(E9), Trajectory(E4),│
│                              │            │ Pattern(E6)                 │
│ Complex middlegame (many     │ +45-80 Elo │ ESA(E1), Asymmetric(E3),    │
│ pieces, many captures)       │            │ Explosion prevention        │
│ Horizon-effect-prone         │ +50-90 Elo │ Boundary(E7), SVF(E5),      │
│                              │            │ ThreatDAG(E2)               │
└──────────────────────────────┴────────────┴──────────────────────────────┘
```

---

## XIII. Computational Budget — DQRS-X

```
┌──────────────────────────────────────┬────────────┬───────────────────┐
│ Component                            │ Time (μs)  │ Frequency         │
├──────────────────────────────────────┼────────────┼───────────────────┤
│ DQRS Base overhead per node          │ 3.0-8.0    │ Every QS node     │
│                                      │            │                   │
│ Ext 1: ESA exchange analysis         │ 2.0-6.0    │ Per exchange start│
│ Ext 1: ESA optimized search          │ -40-60%    │ Node savings      │
│ Ext 2: Threat DAG construction       │ 3.0-10.0   │ Per QS entry      │
│ Ext 2: Chain priority reorder        │ 0.5-1.5    │ Per QS entry      │
│ Ext 3: Asymmetric allocation         │ 0.5-1.5    │ Per QS entry      │
│ Ext 4: Trajectory record + predict   │ 0.3-1.0    │ Per QS node       │
│ Ext 5: Sacrifice classification      │ 1.0-3.0    │ Per neg-SEE move  │
│ Ext 5: Compensation estimation       │ 2.0-5.0    │ Per sacrifice     │
│ Ext 6: Pattern probe                 │ 0.5-2.0    │ Per QS entry      │
│ Ext 6: Pattern store                 │ 0.5-1.5    │ Per QS exit       │
│ Ext 7: Boundary config               │ 0.2-0.5    │ Per transition    │
│ Ext 8: QS-TT probe/store            │ 0.1-0.3    │ Per QS node       │
│ Ext 9: Defense resource counting     │ 1.5-4.0    │ Per threat        │
├──────────────────────────────────────┼────────────┼───────────────────┤
│ DQRS-X total overhead per QS node    │ 5.0-15.0   │                   │
│ Stockfish QS per node                │ 1.5-3.0    │                   │
│ Overhead increase                    │ +200-400%  │                   │
├──────────────────────────────────────┼────────────┼───────────────────┤
│ BUT: Node savings from all exts      │ -35-55%    │                   │
│ ESA exchange optimization            │ -40-60%    │ Exchange sequences│
│ Trajectory early stopping            │ -15-25%    │ Converging pos.   │
│ Pattern memory hits                  │ -10-20%    │ Known patterns    │
│ Defense-directed search              │ -5-15%     │ Clear defense     │
│ QS-TT hits                          │ -10-20%    │ Transpositions    │
├──────────────────────────────────────┼────────────┼───────────────────┤
│ NET per-node cost                    │ +50-100%   │                   │
│ NET total QSearch cost (fewer nodes) │ -10-25%    │                   │
│ NET accuracy improvement             │ +8-15%     │                   │
│ NET effective search depth gain      │ +0.5-1.5   │ Equivalent plies  │
│                                      │            │                   │
│ Memory overhead:                     │            │                   │
│   QSearch TT                         │ 64 MB      │ Persistent        │
│   Pattern Memory                     │ 8-16 MB    │ Persistent        │
│   ESA cache                          │ 1-2 MB     │ Per search        │
│   Threat DAG                         │ 32-64 KB   │ Per QS entry      │
│   Trajectory buffer                  │ 1-2 KB     │ Per QS call       │
│   Total additional memory            │ ~75-85 MB  │                   │
└──────────────────────────────────────┴────────────┴───────────────────┘
```

---

## XIV. Cross-Extension Synergies

```
┌────────────────────────────────────────────────────────────────────────┐
│                     EXTENSION INTERACTION MAP                          │
│                                                                        │
│  ESA (E1) ←→ Threat DAG (E2)                                         │
│    ESA identifies exchange sequences                                   │
│    DAG identifies threats that interact with exchanges                 │
│    Together: detect when threat chain DISRUPTS an exchange             │
│    Synergy bonus: +5-8 Elo                                            │
│                                                                        │
│  Trajectory (E4) ←→ Asymmetric Depth (E3)                            │
│    Trajectory detects which side's eval is more volatile               │
│    Asymmetric allocator uses this for better depth splitting           │
│    Together: dynamic rebalancing as search progresses                  │
│    Synergy bonus: +3-5 Elo                                            │
│                                                                        │
│  SVF (E5) ←→ Threat DAG (E2)                                         │
│    SVF classifies sacrifice type                                       │
│    DAG shows which threat chains sacrifice enables/breaks              │
│    Together: "sacrifice breaks their defensive chain" detection        │
│    Synergy bonus: +5-10 Elo                                           │
│                                                                        │
│  Pattern Memory (E6) ←→ QS-TT (E8)                                   │
│    Patterns are ABSTRACT (position-independent)                        │
│    TT is CONCRETE (position-specific)                                  │
│    Two-level cache: TT for exact positions, patterns for similar       │
│    Synergy bonus: +3-5 Elo                                            │
│                                                                        │
│  Defense Resources (E9) ←→ Threat DAG (E2)                            │
│    Resources counted per-threat                                        │
│    DAG shows cascading: "if this defense used, new threat emerges"     │
│    Together: "this defense resolves threat A but creates threat B"     │
│    Synergy bonus: +4-7 Elo                                            │
│                                                                        │
│  Boundary Smoothing (E7) ←→ Trajectory (E4)                          │
│    Smoothing decides transition zone behavior                          │
│    Trajectory from main search predicts QSearch needs                  │
│    Together: seamless depth-adaptive transition                        │
│    Synergy bonus: +2-4 Elo                                            │
│                                                                        │
│  ESA (E1) ←→ QS-TT (E8)                                              │
│    ESA computes exchange results                                       │
│    TT stores: "exchange on d5 = +42 (verified at depth 6)"           │
│    Reuse: skip ESA analysis if TT has exchange result                  │
│    Synergy bonus: +2-4 Elo                                            │
│                                                                        │
│  Total synergy bonus estimate: +20-40 Elo                             │
│  (Already included in main estimates above)                           │
└────────────────────────────────────────────────────────────────────────┘
```

---

## XV. Lộ Trình Triển Khai Mở Rộng

```
Phase 1 (Month 1-3): DQRS Base + Quick Wins
├── DQRS Base (from original document)
├── Ext 8: QSearch TT Optimization (quick, high-confidence)
├── Ext 7: Boundary Smoothing (moderate complexity)
├── Ext 4: Trajectory Prediction (basic version — convergence only)
└── Target: +25-40 Elo

Phase 2 (Month 4-6): Exchange & Depth Intelligence
├── Ext 1: Exchange Sequence Algebra (core analysis + search)
├── Ext 3: Asymmetric Depth Allocation
├── Ext 4: Full trajectory (divergence, adaptive delta)
├── Integration: ESA ↔ QS-TT
└── Target: +40-60 Elo

Phase 3 (Month 7-9): Threat Intelligence
├── Ext 2: Threat Chain DAG (construction + prioritization)
├── Ext 9: Defensive Resource Counting
├── Integration: DAG ↔ Defense Resources
├── Integration: DAG ↔ ESA (exchange-chain interaction)
└── Target: +55-80 Elo

Phase 4 (Month 10-12): Sacrifice & Patterns
├── Ext 5: Sacrifice Validation Framework
├── Ext 6: Tactical Pattern Memory (core patterns)
├── Integration: SVF ↔ DAG (sacrifice-chain interaction)
├── Integration: Patterns ↔ QS-TT (two-level cache)
└── Target: +65-95 Elo

Phase 5 (Month 13-15): Optimization & Full Integration
├── All cross-extension synergies
├── SIMD/bitboard optimization for tension/threat computation
├── Memory optimization (compact data structures)
├── Fast path optimization (lazy computation paths)
├── Performance profiling and bottleneck removal
└── Target: +60-85 Elo (production, after overhead deduction)

Phase 6 (Month 16-18): Advanced & Refinement
├── Ext 6: Advanced patterns (sacrifice patterns, endgame patterns)
├── Ext 1: ESA with multi-square exchange chains
├── Ext 2: 3+ depth threat chain prediction
├── Neural-assisted resolution scoring (optional)
├── Parameter tuning via large-scale self-play
├── Tournament validation (CCRL/TCEC conditions)
└── Target: +65-95 Elo (final, stable)
```

---

## XVI. So Sánh Tổng Hợp

```
┌─────────────────────────┬──────────────┬──────────────┬──────────────────┐
│ Aspect                  │ Stockfish QS │ DQRS Base    │ DQRS-X           │
├─────────────────────────┼──────────────┼──────────────┼──────────────────┤
│ Move types searched     │ Captures     │ Captures +   │ Captures +       │
│                         │ only         │ critical     │ critical quiets  │
│                         │              │ quiets       │ + ESA exchanges  │
│                         │              │              │ + validated sacs  │
│                         │              │              │                  │
│ Threat understanding    │ None         │ Threat       │ Threat DAG +     │
│                         │              │ inventory    │ cascading chains │
│                         │              │              │ + defense count  │
│                         │              │              │                  │
│ Exchange handling       │ Individual   │ Individual   │ Structured ESA   │
│                         │ captures     │ captures +   │ with zwischenzug │
│                         │              │ zwischenzug  │ + deviation-only │
│                         │              │              │ search           │
│                         │              │              │                  │
│ Sacrifice handling      │ SEE prune    │ Compensation │ Type-classified  │
│                         │              │ estimate     │ SVF with         │
│                         │              │              │ targeted verify  │
│                         │              │              │                  │
│ Depth control           │ Fixed limit  │ Adaptive +   │ Asymmetric +     │
│                         │              │ convergence  │ trajectory-aware │
│                         │              │              │ + DAG-informed   │
│                         │              │              │                  │
│ Eval accuracy           │ ~85-92%      │ ~92-97%      │ ~95-99%          │
│                         │              │              │ (estimated)      │
│                         │              │              │                  │
│ Main↔QS transition      │ Abrupt       │ Abrupt       │ Smooth gradient  │
│                         │              │              │ over 4 plies     │
│                         │              │              │                  │
│ Learning / Memory       │ None         │ None         │ Pattern memory + │
│                         │              │              │ QSearch TT +     │
│                         │              │              │ ESA cache        │
│                         │              │              │                  │
│ Horizon effect          │ Significant  │ Reduced      │ Greatly reduced  │
│                         │              │              │ (multi-layer     │
│                         │              │              │ mitigation)      │
│                         │              │              │                  │
│ Per-node cost           │ 1.5-3μs      │ 3-8μs        │ 5-15μs           │
│ Nodes searched          │ baseline     │ -25-40%      │ -35-55%          │
│ Net search efficiency   │ baseline     │ +10-20%      │ +15-30%          │
│ Estimated Elo gain      │ baseline     │ +30-55       │ +55-95           │
└─────────────────────────┴──────────────┴──────────────┴──────────────────┘
```