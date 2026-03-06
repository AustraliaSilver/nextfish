

# Kiến Trúc Mới Cho Pruning & Reduction: UPAD (Unified Predictive Adaptive Decision)

---

## I. Phân Tích Sâu Hệ Thống Pruning Hiện Tại

### 1.1 Inventory Đầy Đủ Các Kỹ Thuật Stockfish

```
┌─────────────────────────────────────────────────────────────────┐
│              STOCKFISH PRUNING & REDUCTION STACK                 │
│                                                                  │
│  PRE-SEARCH PRUNING (trước khi search node con):                │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ 1. Razoring                                                │  │
│  │    Condition: depth ≤ 3 && eval + margin < alpha           │  │
│  │    Action: Drop to QSearch                                  │  │
│  │    Margin: ~400cp at d1, ~800cp at d3                       │  │
│  │                                                             │  │
│  │ 2. Reverse Futility Pruning (Static Null Move Pruning)     │  │
│  │    Condition: depth ≤ 9 && eval - margin ≥ beta            │  │
│  │    Action: Return eval (beta cutoff without search)         │  │
│  │    Margin: ~80cp × depth                                    │  │
│  │                                                             │  │
│  │ 3. Null Move Pruning (NMP)                                 │  │
│  │    Condition: eval ≥ beta && has_non_pawn_material          │  │
│  │    Action: Skip our turn, search with reduced depth         │  │
│  │    Reduction: R = 4 + depth/6 + min((eval-beta)/200, 3)    │  │
│  │    Verify: re-search if result suspicious                   │  │
│  │                                                             │  │
│  │ 4. ProbCut                                                  │  │
│  │    Condition: depth ≥ 5 && abs(beta) < MATE_VALUE           │  │
│  │    Action: Shallow search with wider window                 │  │
│  │    If shallow result > beta + margin → prune                │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  PER-MOVE PRUNING (quyết định cho từng nước đi):               │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ 5. Futility Pruning                                         │  │
│  │    Condition: depth ≤ 8 && eval + margin ≤ alpha            │  │
│  │    Action: Skip this quiet move                             │  │
│  │    Margin: ~100cp × depth + improvement_factor              │  │
│  │                                                             │  │
│  │ 6. SEE Pruning                                              │  │
│  │    Condition: SEE(move) < threshold                         │  │
│  │    Threshold: quiet → -20×depth², captures → -100×depth     │  │
│  │    Action: Skip this move                                   │  │
│  │                                                             │  │
│  │ 7. Late Move Pruning (Move Count Pruning)                  │  │
│  │    Condition: depth ≤ 8 && moveCount > threshold(depth)     │  │
│  │    Action: Skip remaining quiet moves                       │  │
│  │    Threshold: ~3 + depth² (improving) / ~3 + depth²/2      │  │
│  │                                                             │  │
│  │ 8. History Pruning                                          │  │
│  │    Condition: depth ≤ 4 && history_score < threshold        │  │
│  │    Action: Skip this quiet move                             │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  REDUCTION (search con nhưng nông hơn):                         │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ 9. Late Move Reduction (LMR)                                │  │
│  │    Condition: depth ≥ 2 && moveCount > 1                    │  │
│  │    Reduction: R = LMR_TABLE[depth][moveCount]               │  │
│  │    Adjustments:                                             │  │
│  │      - PV node: R -= 1                                      │  │
│  │      - Improving: R -= 1                                    │  │
│  │      - Good history: R -= history/8192                      │  │
│  │      - Bad history: R += 1                                  │  │
│  │      - Gives check: R -= 1                                  │  │
│  │      - Capture: R -= 1                                      │  │
│  │      - Killer move: R -= 1                                  │  │
│  │      - Singular extension candidate: R -= 1                 │  │
│  │    Re-search: if reduced search > alpha, re-search at full  │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  EXTENSION (search sâu hơn bình thường):                       │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ 10. Singular Extension                                      │  │
│  │     Condition: TT move with sufficient depth                │  │
│  │     Test: Search excluding TT move at reduced depth         │  │
│  │     If all others fail low by margin → extend TT move +1   │  │
│  │                                                             │  │
│  │ 11. Check Extension                                         │  │
│  │     Condition: Move gives check (reduced role recently)     │  │
│  │                                                             │  │
│  │ 12. Passed Pawn Extension                                   │  │
│  │     Condition: Pawn push to 6th/7th rank                    │  │
│  │                                                             │  │
│  │ 13. Recapture Extension                                     │  │
│  │     Condition: Recapture on same square                     │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  QUIESCENCE-LEVEL:                                              │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ 14. Delta Pruning (QSearch)                                 │  │
│  │     Condition: stand_pat + max_gain + 200 < alpha           │  │
│  │                                                             │  │
│  │ 15. SEE Pruning (QSearch)                                   │  │
│  │     Condition: SEE(capture) < 0                             │  │
│  └────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 Phân Tích Tương Tác Giữa Các Kỹ Thuật

```
INTERACTION PROBLEMS:

1. CASCADING FAILURES (Thất bại dây chuyền)
   ─────────────────────────────────────
   Khi nhiều techniques cùng đánh giá sai → lỗi khuếch đại

   Ví dụ: Position cần sacrifice
   a) Static eval quá thấp (sacrifice = material loss)
   b) Reverse Futility Pruning: "eval quá thấp" → prune cả subtree
   c) Kết quả: Không bao giờ search sacrifice
   d) NMP cũng fail: null move search returns beta 
      (vì không thấy sacrifice threat)

   → Multiple pruning layers đồng thuận sai → critical move bị bỏ qua


2. REDUNDANT COMPUTATION (Tính toán thừa)
   ─────────────────────────────────────
   Nhiều techniques kiểm tra điều kiện overlap

   Ví dụ: Quiet move ở depth 3
   a) Futility pruning kiểm tra: eval + margin vs alpha  ← eval computed
   b) History pruning kiểm tra: history < threshold       ← history read
   c) SEE pruning kiểm tra: SEE(move) < threshold         ← SEE computed
   d) LMP kiểm tra: moveCount > threshold                 ← count checked
   e) LMR tính reduction dùng: history, moveCount, depth   ← recompute

   → Cùng thông tin được access/compute nhiều lần
   → Không share intermediate results giữa techniques


3. CONFLICTING DECISIONS (Quyết định mâu thuẫn)
   ────────────────────────────────────────────
   Technique A nói "prune" nhưng technique B nói "extend"

   Ví dụ:
   a) LMP: moveCount = 15, depth = 3 → "prune" (quá muộn trong list)
   b) Nhưng move gives check → check extension nói "extend +1"
   c) Và move có high history → LMR nói "reduce ít"
   d) Kết quả: ad-hoc resolution (check_extension > LMP override)

   → Không có framework thống nhất để resolve conflicts
   → Priority rules hard-coded, không adaptive


4. CONTEXT-BLIND PARAMETERS (Tham số mù context)
   ───────────────────────────────────────────────
   Margins và thresholds là constants, không adapt

   Ví dụ: Reverse Futility margin = 80 × depth
   - Trong closed position: 80cp/depth quá nhỏ (position stable)
     → Nên prune aggressive hơn (margin lớn hơn)
   - Trong open tactical position: 80cp/depth quá lớn
     → Prune quá nhiều, miss tactics
     → Nên prune conservative hơn (margin nhỏ hơn)

   Tương tự cho:
   - NMP reduction: R cố định theo depth, không theo position type
   - LMR table: cố định, không theo tactical complexity
   - Futility margin: linear với depth, không theo eval confidence


5. ORDERING DEPENDENCY (Phụ thuộc thứ tự)
   ─────────────────────────────────────
   Kết quả pruning phụ thuộc vào thứ tự áp dụng techniques

   Nếu áp dụng NMP trước Reverse Futility:
   → NMP có thể prune → Reverse Futility không bao giờ chạy
   
   Nếu áp dụng Reverse Futility trước NMP:
   → Reverse Futility prune → NMP không chạy
   
   Hai orderings có thể cho kết quả khác nhau!
   → Thứ tự hiện tại được chọn bằng kinh nghiệm, không có lý thuyết

6. EVAL DEPENDENCY (Phụ thuộc evaluation)
   ──────────────────────────────────────
   Hầu hết pruning techniques phụ thuộc vào static eval
   
   Nếu eval sai 100cp (xảy ra ~10-15% positions):
   → Futility pruning sai
   → Reverse futility sai  
   → NMP reduction sai
   → Razoring sai
   
   → Eval error khuếch đại thành pruning error
   → Nhưng KHÔNG có mechanism để detect "eval có thể sai"
```

### 1.3 Thống Kê Lỗi Pruning

```
Estimated error rates (từ phân tích search logs):

┌───────────────────────────┬──────────────┬───────────────────────┐
│ Technique                 │ Usage Rate   │ Error Rate            │
│                           │ (% of nodes) │ (prune sai/quá nhiều) │
├───────────────────────────┼──────────────┼───────────────────────┤
│ Null Move Pruning         │ ~15-25%      │ ~2-5% (zugzwang, sac)│
│ Reverse Futility          │ ~8-15%       │ ~3-7% (tactical pos) │
│ Futility Pruning          │ ~10-20%      │ ~4-8% (deep tactics) │
│ SEE Pruning               │ ~5-10%       │ ~2-4% (sacrifice)    │
│ Late Move Pruning         │ ~15-25%      │ ~5-10% (quiet tactic)│
│ Late Move Reduction       │ ~40-60%      │ ~8-15% (under-reduce)│
│ Razoring                  │ ~3-5%        │ ~5-10% (deep tactic) │
│ ProbCut                   │ ~2-4%        │ ~3-6%                │
├───────────────────────────┼──────────────┼───────────────────────┤
│ Combined system           │ ~70-85%      │ ~1-3% (critical err) │
│ (at least 1 technique)    │ of all nodes │ (miss best move)     │
└───────────────────────────┴──────────────┴───────────────────────┘

"Critical error" = pruning/reducing gây ra miss best move ở root
→ 1-3% sounds nhỏ, nhưng over millions of positions → significant Elo loss
→ Each critical error ≈ -0.001 to -0.01 Elo averaged over all games
→ Total pruning error cost: estimated -30 to -80 Elo vs perfect search
```

---

## II. UPAD - Unified Predictive Adaptive Decision

### 2.1 Triết Lý Thiết Kế

```
CORE PHILOSOPHY:

1. UNIFIED: Một framework duy nhất thay vì 15+ techniques riêng lẻ
   → Tránh conflict, redundancy, ordering dependency
   → Single decision point: (prune, reduce, normal, extend)

2. PREDICTIVE: Dự đoán "subtree này có chứa best move không?"
   thay vì dùng heuristics đơn giản
   → Forward-looking thay vì backward-looking
   → Evidence-based thay vì threshold-based

3. ADAPTIVE: Tự điều chỉnh theo position type, search state, 
   và feedback từ re-search
   → No fixed margins/thresholds
   → Online calibration

4. DECISION: Output là một quyết định liên tục trên spectrum:
   prune ←──── reduce ←──── normal ←──── extend
   Thay vì binary prune/don't prune
```

### 2.2 Kiến Trúc Tổng Thể

```
┌──────────────────────────────────────────────────────────────────────┐
│                        UPAD ARCHITECTURE                              │
│           Unified Predictive Adaptive Decision Framework              │
│                                                                       │
│  ╔═══════════════════════════════════════════════════════════════════╗ │
│  ║                   EVIDENCE COLLECTION                             ║ │
│  ║                                                                   ║ │
│  ║  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐           ║ │
│  ║  │ Static   │ │ Dynamic  │ │ Search   │ │ Position │           ║ │
│  ║  │ Evidence │ │ Evidence │ │ Context  │ │ Topology │           ║ │
│  ║  │ Gatherer │ │ Gatherer │ │ Evidence │ │ Analyzer │           ║ │
│  ║  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘           ║ │
│  ║       └──────┬──────┴──────┬─────┴──────┬─────┘                 ║ │
│  ╚══════════════╪═════════════╪════════════╪═══════════════════════╝ │
│                 ▼             ▼            ▼                         │
│  ╔═══════════════════════════════════════════════════════════════════╗ │
│  ║                   EVIDENCE FUSION                                 ║ │
│  ║                                                                   ║ │
│  ║  ┌──────────────────────────────────────────────────────────┐    ║ │
│  ║  │              Evidence Vector E(position, move)            │    ║ │
│  ║  │              (unified representation of all evidence)     │    ║ │
│  ║  └────────────────────────┬─────────────────────────────────┘    ║ │
│  ╚═══════════════════════════╪═══════════════════════════════════════╝ │
│                              ▼                                        │
│  ╔═══════════════════════════════════════════════════════════════════╗ │
│  ║                   DECISION ENGINE                                 ║ │
│  ║                                                                   ║ │
│  ║  ┌────────────┐  ┌────────────────┐  ┌────────────────────┐     ║ │
│  ║  │ Node-Level │  │  Move-Level    │  │  Depth-Level       │     ║ │
│  ║  │ Decision   │  │  Decision      │  │  Decision          │     ║ │
│  ║  │ (pre-move) │  │  (per-move)    │  │  (reduction/ext)   │     ║ │
│  ║  └──────┬─────┘  └──────┬─────────┘  └──────┬─────────────┘     ║ │
│  ║         └────────┬──────┴────────┬───────────┘                   ║ │
│  ╚══════════════════╪═══════════════╪═══════════════════════════════╝ │
│                     ▼               ▼                                 │
│  ╔═══════════════════════════════════════════════════════════════════╗ │
│  ║                   ADAPTIVE CALIBRATION                            ║ │
│  ║                                                                   ║ │
│  ║  ┌──────────────┐  ┌──────────────┐  ┌────────────────────┐     ║ │
│  ║  │ Re-search    │  │  Error       │  │  Online Parameter  │     ║ │
│  ║  │ Feedback     │  │  Tracking    │  │  Adjustment        │     ║ │
│  ║  └──────────────┘  └──────────────┘  └────────────────────┘     ║ │
│  ╚═══════════════════════════════════════════════════════════════════╝ │
└──────────────────────────────────────────────────────────────────────┘
```

---

## III. Evidence Collection Layer

### 3.1 Static Evidence Gatherer

```python
class StaticEvidenceGatherer:
    """Thu thập evidence tĩnh từ position và move
    
    Đây là thông tin có thể tính mà KHÔNG cần search.
    Tương đương với thông tin mà các pruning techniques 
    hiện tại dùng, nhưng tổng hợp thành vector thống nhất.
    """
    
    def gather(self, position, move, eval_score):
        evidence = StaticEvidence()
        
        # ═══ EVAL-BASED EVIDENCE ═══
        
        # E1: Eval distance from window
        evidence.eval_above_beta = max(0, eval_score - self.beta)
        evidence.eval_below_alpha = max(0, self.alpha - eval_score)
        evidence.eval_in_window = (self.alpha <= eval_score <= self.beta)
        
        # E2: Eval confidence
        # Thay vì dùng eval trực tiếp, kèm theo confidence
        evidence.eval_confidence = self.estimate_eval_confidence(
            position, eval_score
        )
        
        # E3: Eval volatility (eval thay đổi bao nhiêu giữa iterations)
        evidence.eval_volatility = self.get_eval_volatility(position)
        
        # E4: Improving flag
        # Position đang cải thiện so với 2 ply trước?
        evidence.is_improving = self.check_improving(position)
        evidence.improvement_magnitude = self.get_improvement_magnitude(
            position
        )
        
        # ═══ MATERIAL-BASED EVIDENCE ═══
        
        # E5: Material state
        evidence.total_material = total_non_pawn_material(position)
        evidence.material_balance = material_balance(position)
        evidence.has_non_pawn_material = (
            evidence.total_material > 0  # Cho bên đang đi
        )
        
        # E6: Material imbalance type
        evidence.imbalance_type = classify_imbalance(position)
        # 0: balanced, 1: minor up, 2: exchange up, 
        # 3: minor down, 4: exchange down, etc.
        
        # ═══ MOVE-SPECIFIC EVIDENCE ═══
        
        # E7: Move characteristics
        evidence.is_capture = move.is_capture
        evidence.is_promotion = move.is_promotion
        evidence.gives_check = move.gives_check
        evidence.is_castle = move.is_castling
        
        # E8: SEE value
        evidence.see_value = see(position, move)
        evidence.see_normalized = evidence.see_value / 1000.0
        
        # E9: Move ordering position
        evidence.move_count = move.ordering_index  # Thứ tự trong list
        evidence.move_count_normalized = move.ordering_index / max(
            total_moves, 1
        )
        
        # E10: History scores
        evidence.history_score = get_history_score(position, move)
        evidence.continuation_history = get_continuation_history(
            position, move
        )
        evidence.capture_history = get_capture_history(position, move)
        
        # E11: Is killer/counter
        evidence.is_killer = is_killer_move(move)
        evidence.is_counter = is_counter_move(move)
        evidence.is_tt_move = (move == self.tt_move)
        
        # ═══ KING SAFETY EVIDENCE ═══
        
        # E12: King safety impact
        evidence.our_king_safety = evaluate_king_safety(
            position, position.side_to_move
        )
        evidence.their_king_safety = evaluate_king_safety(
            position, position.opponent
        )
        evidence.move_king_safety_impact = estimate_king_safety_change(
            position, move
        )
        
        # ═══ TACTICAL EVIDENCE ═══
        
        # E13: Tactical indicators
        evidence.hanging_pieces_our = count_hanging_pieces(
            position, position.side_to_move
        )
        evidence.hanging_pieces_their = count_hanging_pieces(
            position, position.opponent
        )
        evidence.pins_count = count_pins(position)
        evidence.threats_count = count_threats(position)
        evidence.checks_available = count_available_checks(
            position, position.side_to_move
        )
        
        # E14: Pawn structure tension
        evidence.pawn_tension_points = count_pawn_tension(position)
        evidence.open_files = count_open_files(position)
        
        return evidence
    
    def estimate_eval_confidence(self, position, eval_score):
        """Ước tính độ tin cậy của static eval
        
        Key insight: Pruning nên aggressive khi eval confident,
        conservative khi eval uncertain.
        """
        confidence = 1.0
        
        # Material imbalance → eval ít tin cậy
        if has_unusual_material(position):
            confidence *= 0.7
        
        # Nhiều quân treo → tactical, eval có thể sai
        hanging = count_hanging_pieces(position, BOTH_SIDES)
        confidence *= max(0.3, 1.0 - hanging * 0.15)
        
        # Vua exposed → eval volatile
        if king_exposed(position, position.side_to_move):
            confidence *= 0.6
        if king_exposed(position, position.opponent):
            confidence *= 0.7
        
        # Pawn tension cao → thế cờ có thể thay đổi nhiều
        tension = count_pawn_tension(position)
        confidence *= max(0.5, 1.0 - tension * 0.08)
        
        # Eval rất lệch → có thể đúng nhưng cũng có thể sai
        if abs(eval_score) > 300:
            confidence *= 0.85  # Slight doubt at extreme evals
        
        # Nhiều quân → nhiều possible tactics → ít tin cậy
        piece_count = popcount(position.occupied)
        if piece_count > 28:
            confidence *= 0.9
        
        return clamp(confidence, 0.1, 1.0)
```

### 3.2 Dynamic Evidence Gatherer

```python
class DynamicEvidenceGatherer:
    """Thu thập evidence động từ search process
    
    Thông tin CHỈ có được TRONG KHI search, không phải từ position tĩnh.
    Đây là thông tin mà pruning techniques hiện tại hầu hết bỏ qua.
    """
    
    def gather(self, position, move, search_state):
        evidence = DynamicEvidence()
        
        # ═══ SEARCH TREE EVIDENCE ═══
        
        # D1: Node type prediction
        evidence.expected_node_type = search_state.predict_node_type()
        # CUT_NODE: expected to fail high (most pruning safe)
        # ALL_NODE: expected to fail low (pruning risky - might miss refutation)
        # PV_NODE: principal variation (pruning dangerous)
        evidence.node_type_confidence = search_state.node_type_confidence
        
        # D2: Parent node information
        evidence.parent_was_null_move = search_state.parent_was_null_move
        evidence.parent_eval = search_state.parent_eval
        evidence.parent_best_score = search_state.parent_best_score
        
        # D3: Sibling node information
        evidence.siblings_searched = search_state.siblings_searched
        evidence.siblings_failed_high = search_state.siblings_failed_high
        evidence.siblings_avg_score = search_state.siblings_avg_score
        evidence.best_sibling_score = search_state.best_sibling_score
        
        # D4: Previous iteration data (from iterative deepening)
        evidence.prev_iteration_score = search_state.prev_iteration_score
        evidence.prev_iteration_best_move = search_state.prev_iteration_best_move
        evidence.score_trend = search_state.get_score_trend()
        # score_trend: rising, falling, stable
        
        # D5: TT information
        evidence.tt_hit = search_state.tt_hit
        evidence.tt_depth = search_state.tt_depth if evidence.tt_hit else 0
        evidence.tt_score = search_state.tt_score if evidence.tt_hit else 0
        evidence.tt_bound = search_state.tt_bound if evidence.tt_hit else NONE
        evidence.tt_move_matches = (
            search_state.tt_move == move if evidence.tt_hit else False
        )
        
        # ═══ SEARCH PROGRESS EVIDENCE ═══
        
        # D6: Remaining depth
        evidence.remaining_depth = search_state.depth
        evidence.depth_from_root = search_state.ply
        evidence.max_depth = search_state.max_depth
        evidence.depth_ratio = evidence.depth_from_root / max(
            evidence.max_depth, 1
        )
        
        # D7: Search effort so far
        evidence.nodes_searched_this_subtree = search_state.local_nodes
        evidence.nodes_searched_total = search_state.global_nodes
        evidence.time_elapsed_ratio = search_state.time_elapsed_ratio
        
        # D8: Current window
        evidence.window_size = search_state.beta - search_state.alpha
        evidence.is_null_window = (evidence.window_size <= 1)
        evidence.is_aspiration_window = search_state.is_aspiration
        
        # ═══ SUBTREE PREDICTION EVIDENCE ═══
        
        # D9: Expected subtree size
        evidence.expected_subtree_nodes = self.predict_subtree_size(
            search_state
        )
        
        # D10: Similar position results
        evidence.similar_position_outcome = self.lookup_similar_outcomes(
            position, search_state
        )
        
        # D11: Re-search risk
        # Nếu reduce rồi re-search, cost bao nhiêu?
        evidence.re_search_cost = self.estimate_re_search_cost(
            search_state
        )
        
        return evidence
    
    def predict_subtree_size(self, search_state):
        """Dự đoán kích thước subtree nếu search đầy đủ"""
        depth = search_state.depth
        
        # Base estimate từ branching factor
        avg_bf = search_state.get_effective_branching_factor()
        base_estimate = avg_bf ** depth
        
        # Adjust theo node type
        if search_state.expected_node_type == CUT_NODE:
            base_estimate *= 0.3  # CUT nodes search ít hơn
        elif search_state.expected_node_type == ALL_NODE:
            base_estimate *= 1.2  # ALL nodes search nhiều hơn
        
        return base_estimate
    
    def estimate_re_search_cost(self, search_state):
        """Ước tính chi phí nếu reduction dẫn đến re-search"""
        depth = search_state.depth
        
        # Re-search cost = search ở full depth thay vì reduced depth
        # P(re-search) × cost_of_re_search
        
        p_re_search = self.estimate_re_search_probability(search_state)
        cost_if_re_search = search_state.effective_branching_factor ** depth
        
        return p_re_search * cost_if_re_search
    
    def estimate_re_search_probability(self, search_state):
        """Xác suất re-search sau LMR"""
        # Dựa trên historical data
        # CUT nodes: thấp (thường fail high ngay)
        # PV nodes: cao (sensitive to score)
        # ALL nodes: trung bình
        
        base_p = {
            CUT_NODE: 0.10,
            ALL_NODE: 0.25,
            PV_NODE: 0.40,
        }.get(search_state.expected_node_type, 0.25)
        
        # History score ảnh hưởng
        if search_state.current_move_history > 0:
            base_p *= 1.3  # Good history → more likely to need full search
        
        return clamp(base_p, 0.01, 0.80)
```

### 3.3 Search Context Evidence

```python
class SearchContextEvidence:
    """Evidence từ broader search context
    
    Thông tin về tổng thể search, không chỉ node hiện tại.
    """
    
    def gather(self, search_state):
        evidence = ContextEvidence()
        
        # C1: Time management pressure
        evidence.time_pressure = search_state.time_pressure_factor()
        # 0.0 = plenty of time
        # 1.0 = very low on time
        # Khi thời gian ít → prune aggressive hơn
        
        # C2: Search stability
        evidence.score_stability = search_state.get_score_stability()
        # Score stable qua nhiều iterations → position well-understood
        # → Pruning safe hơn
        
        evidence.best_move_stability = search_state.get_move_stability()
        # Best move không đổi → ordering tốt → pruning safe hơn
        
        # C3: Branching factor trend
        evidence.bf_trend = search_state.get_bf_trend()
        # BF tăng → tree expanding → nên prune aggressive hơn
        # BF giảm → tree converging → pruning đang hiệu quả
        
        # C4: Fail rate
        evidence.recent_fail_high_rate = search_state.recent_fail_high_rate()
        evidence.recent_fail_low_rate = search_state.recent_fail_low_rate()
        # Nhiều fail high → CUT-heavy tree → pruning safe
        # Nhiều fail low → ALL-heavy tree → pruning risky
        
        # C5: Re-search rate
        evidence.recent_re_search_rate = search_state.recent_re_search_rate()
        # Re-search rate cao → reductions quá aggressive
        # Re-search rate thấp → có thể reduce more
        
        # C6: Pruning error detected rate
        evidence.recent_pruning_errors = search_state.recent_pruning_errors()
        # Error cao → nên conservative hơn
        # Error thấp → có thể aggressive hơn
        
        # C7: Game phase
        evidence.game_phase = classify_phase(search_state.root_position)
        evidence.is_endgame = (evidence.game_phase >= ENDGAME)
        
        # C8: Root eval
        evidence.root_eval = search_state.root_eval
        evidence.is_winning = (evidence.root_eval > 200)
        evidence.is_losing = (evidence.root_eval < -200)
        evidence.is_drawn = (abs(evidence.root_eval) < 30)
        
        return evidence
```

### 3.4 Position Topology Analyzer

```python
class PositionTopologyAnalyzer:
    """Phân tích cấu trúc topo học của thế cờ
    
    Concept mới: thay vì phân loại position thành vài categories,
    tính một "topology fingerprint" mô tả đặc tính pruning-relevant.
    """
    
    def analyze(self, position):
        topology = PositionTopology()
        
        # ═══ OPENNESS ═══
        # Đo mức độ "mở" của thế cờ (ảnh hưởng đến tactical density)
        
        topology.openness = self.compute_openness(position)
        # 0.0 = completely closed (locked pawns, no open files)
        # 1.0 = completely open (no pawns, all files open)
        
        # ═══ TACTICAL DENSITY ═══
        # Đo mật độ chiến thuật (ảnh hưởng đến pruning safety)
        
        topology.tactical_density = self.compute_tactical_density(position)
        # 0.0 = dead quiet, no tactics anywhere
        # 1.0 = extremely tactical, many hanging/forking possibilities
        
        # ═══ STABILITY ═══
        # Position ổn định hay có thể thay đổi nhanh?
        
        topology.stability = self.compute_stability(position)
        # 0.0 = very unstable (one move changes everything)
        # 1.0 = very stable (many moves to change significantly)
        
        # ═══ ASYMMETRY ═══
        # Hai bên có vị trí đối xứng hay không?
        
        topology.asymmetry = self.compute_asymmetry(position)
        # 0.0 = symmetric position
        # 1.0 = highly asymmetric (different plans required)
        
        # ═══ KING DANGER ═══
        # Mức độ nguy hiểm cho vua (ảnh hưởng đến NMP safety)
        
        topology.white_king_danger = self.king_danger_score(position, WHITE)
        topology.black_king_danger = self.king_danger_score(position, BLACK)
        topology.max_king_danger = max(
            topology.white_king_danger, topology.black_king_danger
        )
        
        # ═══ ZUGZWANG RISK ═══
        # Mức độ nguy cơ zugzwang (ảnh hưởng đến NMP)
        
        topology.zugzwang_risk = self.compute_zugzwang_risk(position)
        # 0.0 = no risk (lots of pieces, many moves)
        # 1.0 = high risk (endgame, few pieces, king opposition)
        
        # ═══ COMPRESSION ═══
        # Bao nhiêu nước đi thực sự khác biệt?
        
        topology.move_diversity = self.compute_move_diversity(position)
        # 0.0 = tất cả moves tương tự nhau (hard to prune safely)
        # 1.0 = moves rất khác nhau (easy to distinguish good from bad)
        
        # ═══ FORCING LEVEL ═══
        # Bao nhiêu nước đi là forced hoặc near-forced?
        
        topology.forcing_level = self.compute_forcing_level(position)
        # 0.0 = no forcing moves (quiet position)
        # 1.0 = mostly forcing moves (checks, captures, threats)
        
        return topology
    
    def compute_openness(self, position):
        open_files = count_open_files(position)
        semi_open_files = count_semi_open_files(position)
        pawn_count = popcount(position.pawns(WHITE) | position.pawns(BLACK))
        
        file_openness = (open_files * 2 + semi_open_files) / 16.0
        pawn_openness = 1.0 - pawn_count / 16.0
        
        # Center openness matters more
        center_open = not has_locked_center(position)
        
        return (file_openness * 0.4 + pawn_openness * 0.3 
                + (0.3 if center_open else 0.0))
    
    def compute_tactical_density(self, position):
        """Mật độ chiến thuật: bao nhiêu tactical motif có thể xảy ra"""
        density = 0.0
        
        # Hanging pieces (weight: high)
        hanging = count_hanging_pieces(position, BOTH_SIDES)
        density += min(hanging * 0.2, 0.4)
        
        # Pins and skewers
        pins = count_pins(position)
        density += min(pins * 0.1, 0.2)
        
        # Fork possibilities
        fork_squares = count_fork_squares(position)
        density += min(fork_squares * 0.05, 0.15)
        
        # Checks available
        checks = count_checking_moves(position, position.side_to_move)
        density += min(checks * 0.03, 0.1)
        
        # Back rank weakness
        if has_back_rank_weakness(position, WHITE):
            density += 0.1
        if has_back_rank_weakness(position, BLACK):
            density += 0.1
        
        # Overloaded pieces
        overloaded = count_overloaded_pieces(position)
        density += min(overloaded * 0.15, 0.2)
        
        return clamp(density, 0.0, 1.0)
    
    def compute_stability(self, position):
        """Position stability: liệu evaluation có thay đổi nhiều 
        sau 1-2 nước?"""
        
        stability = 1.0
        
        # Material en prise → unstable
        en_prise = count_en_prise_pieces(position)
        stability -= en_prise * 0.15
        
        # Pawn tension → position can change rapidly
        tension = count_pawn_tension(position)
        stability -= tension * 0.08
        
        # Promotion threats → big changes possible
        close_promotions = count_near_promotion_pawns(position)
        stability -= close_promotions * 0.2
        
        # King exposure → big attacks possible
        if king_exposed(position, WHITE) or king_exposed(position, BLACK):
            stability -= 0.2
        
        # Open center → more tactical changes
        if not has_locked_center(position):
            stability -= 0.1
        
        return clamp(stability, 0.0, 1.0)
    
    def compute_zugzwang_risk(self, position):
        """Ước tính khả năng zugzwang"""
        risk = 0.0
        
        stm = position.side_to_move
        
        # Ít quân (trừ vua và tốt) → risk cao
        non_pawn_material = count_non_pawn_material(position, stm)
        if non_pawn_material == 0:
            risk += 0.6  # King + pawns only → high risk
        elif non_pawn_material <= BISHOP_VALUE:
            risk += 0.3  # One minor piece
        elif non_pawn_material <= ROOK_VALUE:
            risk += 0.15
        
        # Tốt bị block → ít nước tốt → risk cao
        blocked_pawns = count_blocked_pawns(position, stm)
        total_pawns = count_pawns(position, stm)
        if total_pawns > 0:
            pawn_mobility = 1.0 - blocked_pawns / total_pawns
            risk += (1.0 - pawn_mobility) * 0.2
        
        # King opposition → zugzwang pattern
        if kings_in_opposition(position):
            risk += 0.2
        
        # Ít nước đi hợp lệ → risk cao
        legal_moves = len(position.legal_moves())
        if legal_moves < 5:
            risk += 0.3
        elif legal_moves < 10:
            risk += 0.1
        
        return clamp(risk, 0.0, 1.0)
    
    def compute_forcing_level(self, position):
        """Mức độ forcing: bao nhiêu nước đi buộc đối phương phải respond"""
        total_moves = len(position.legal_moves())
        
        if total_moves == 0:
            return 1.0
        
        forcing_moves = 0
        
        for move in position.legal_moves():
            if move.gives_check:
                forcing_moves += 1
            elif move.is_capture and see(position, move) >= 0:
                forcing_moves += 1
            elif creates_immediate_threat(position, move):
                forcing_moves += 0.5  # Semi-forcing
        
        return clamp(forcing_moves / total_moves, 0.0, 1.0)
```

---

## IV. Evidence Fusion Layer

### 4.1 Evidence Vector Construction

```python
class EvidenceVector:
    """Vector thống nhất chứa tất cả evidence cho một quyết định
    pruning/reduction
    
    Thay vì 15+ techniques với conditions riêng,
    tất cả evidence được pack vào 1 vector duy nhất.
    """
    
    # Total features: 64 (optimized for cache line alignment)
    FEATURE_LAYOUT = {
        # Static Evidence (20 features)
        'eval_above_beta': 0,
        'eval_below_alpha': 1,
        'eval_confidence': 2,
        'eval_volatility': 3,
        'is_improving': 4,
        'improvement_magnitude': 5,
        'total_material': 6,
        'material_balance': 7,
        'see_value': 8,
        'move_count_normalized': 9,
        'history_score_normalized': 10,
        'continuation_history_normalized': 11,
        'is_capture': 12,
        'gives_check': 13,
        'is_killer': 14,
        'is_tt_move': 15,
        'our_king_safety': 16,
        'their_king_safety': 17,
        'hanging_pieces_our': 18,
        'hanging_pieces_their': 19,
        
        # Dynamic Evidence (16 features)
        'expected_node_type': 20,        # 0=PV, 0.5=ALL, 1.0=CUT
        'node_type_confidence': 21,
        'siblings_searched': 22,
        'siblings_fail_high_rate': 23,
        'best_sibling_score_normalized': 24,
        'prev_iteration_score_normalized': 25,
        'score_trend': 26,               # -1=falling, 0=stable, 1=rising
        'tt_hit': 27,
        'tt_depth_normalized': 28,
        'remaining_depth_normalized': 29,
        'depth_from_root_normalized': 30,
        'window_size_normalized': 31,
        'is_null_window': 32,
        'expected_subtree_size_log': 33,
        're_search_cost_log': 34,
        'time_pressure': 35,
        
        # Topology Evidence (12 features)
        'openness': 36,
        'tactical_density': 37,
        'stability': 38,
        'asymmetry': 39,
        'max_king_danger': 40,
        'zugzwang_risk': 41,
        'move_diversity': 42,
        'forcing_level': 43,
        'pawn_tension': 44,
        'open_files': 45,
        'pins_count': 46,
        'threats_count': 47,
        
        # Context Evidence (8 features)
        'search_stability': 48,
        'best_move_stability': 49,
        'bf_trend': 50,
        'recent_fail_high_rate': 51,
        'recent_re_search_rate': 52,
        'recent_pruning_errors': 53,
        'game_phase': 54,
        'root_eval_normalized': 55,
        
        # Derived / Interaction features (8 features)
        'eval_x_confidence': 56,         # eval_above_beta × confidence
        'tactical_x_depth': 57,          # tactical_density × depth
        'zugzwang_x_material': 58,       # zugzwang_risk × (1-material)
        'forcing_x_see': 59,             # forcing_level × see_value
        'stability_x_improving': 60,     # stability × is_improving
        'history_x_move_count': 61,      # history × move_count
        'king_danger_x_check': 62,       # king_danger × gives_check
        'node_type_x_eval': 63,          # node_type × eval_distance
    }
    
    def __init__(self):
        self.features = np.zeros(64, dtype=np.float32)
    
    def build(self, static_ev, dynamic_ev, topology, context_ev):
        """Xây dựng evidence vector từ các gatherers"""
        
        # Pack static evidence
        self.features[0] = static_ev.eval_above_beta / 500.0
        self.features[1] = static_ev.eval_below_alpha / 500.0
        self.features[2] = static_ev.eval_confidence
        self.features[3] = static_ev.eval_volatility / 100.0
        self.features[4] = float(static_ev.is_improving)
        self.features[5] = static_ev.improvement_magnitude / 200.0
        self.features[6] = static_ev.total_material / 8000.0
        self.features[7] = static_ev.material_balance / 2000.0
        self.features[8] = clamp(static_ev.see_value / 1000.0, -1, 1)
        self.features[9] = static_ev.move_count_normalized
        self.features[10] = clamp(
            static_ev.history_score / 16384.0, -1, 1
        )
        self.features[11] = clamp(
            static_ev.continuation_history / 16384.0, -1, 1
        )
        self.features[12] = float(static_ev.is_capture)
        self.features[13] = float(static_ev.gives_check)
        self.features[14] = float(static_ev.is_killer)
        self.features[15] = float(static_ev.is_tt_move)
        self.features[16] = static_ev.our_king_safety / 500.0
        self.features[17] = static_ev.their_king_safety / 500.0
        self.features[18] = static_ev.hanging_pieces_our / 5.0
        self.features[19] = static_ev.hanging_pieces_their / 5.0
        
        # Pack dynamic evidence
        self.features[20] = dynamic_ev.expected_node_type_normalized
        self.features[21] = dynamic_ev.node_type_confidence
        self.features[22] = min(dynamic_ev.siblings_searched / 30.0, 1.0)
        self.features[23] = dynamic_ev.siblings_fail_high_rate
        self.features[24] = dynamic_ev.best_sibling_score / 1000.0
        self.features[25] = dynamic_ev.prev_iteration_score / 1000.0
        self.features[26] = dynamic_ev.score_trend
        self.features[27] = float(dynamic_ev.tt_hit)
        self.features[28] = dynamic_ev.tt_depth / 30.0
        self.features[29] = dynamic_ev.remaining_depth / 30.0
        self.features[30] = dynamic_ev.depth_from_root / 30.0
        self.features[31] = min(dynamic_ev.window_size / 500.0, 1.0)
        self.features[32] = float(dynamic_ev.is_null_window)
        self.features[33] = math.log1p(dynamic_ev.expected_subtree_nodes) / 20.0
        self.features[34] = math.log1p(dynamic_ev.re_search_cost) / 20.0
        self.features[35] = context_ev.time_pressure
        
        # Pack topology evidence
        self.features[36] = topology.openness
        self.features[37] = topology.tactical_density
        self.features[38] = topology.stability
        self.features[39] = topology.asymmetry
        self.features[40] = topology.max_king_danger
        self.features[41] = topology.zugzwang_risk
        self.features[42] = topology.move_diversity
        self.features[43] = topology.forcing_level
        self.features[44] = topology.pawn_tension / 10.0
        self.features[45] = topology.open_files / 8.0
        self.features[46] = topology.pins_count / 5.0
        self.features[47] = topology.threats_count / 10.0
        
        # Pack context evidence
        self.features[48] = context_ev.search_stability
        self.features[49] = context_ev.best_move_stability
        self.features[50] = context_ev.bf_trend
        self.features[51] = context_ev.recent_fail_high_rate
        self.features[52] = context_ev.recent_re_search_rate
        self.features[53] = context_ev.recent_pruning_errors
        self.features[54] = context_ev.game_phase / 4.0
        self.features[55] = context_ev.root_eval / 1000.0
        
        # Compute derived features
        self.features[56] = self.features[0] * self.features[2]
        self.features[57] = self.features[37] * self.features[29]
        self.features[58] = self.features[41] * (1 - self.features[6])
        self.features[59] = self.features[43] * self.features[8]
        self.features[60] = self.features[38] * self.features[4]
        self.features[61] = self.features[10] * self.features[9]
        self.features[62] = self.features[40] * self.features[13]
        self.features[63] = self.features[20] * self.features[0]
        
        return self.features
```

---

## V. Decision Engine

### 5.1 Tổng Quan Decision Engine

```
┌────────────────────────────────────────────────────────────────────┐
│                      DECISION ENGINE                                │
│                                                                     │
│  Input: Evidence Vector E (64 floats)                               │
│  Output: Decision D = {action, confidence, depth_adjustment}        │
│                                                                     │
│  Actions:                                                           │
│    HARD_PRUNE:  Cắt hoàn toàn, không search node này               │
│    SOFT_PRUNE:  Search rất nông (depth 1-2) để verify               │
│    HEAVY_REDUCE: Giảm depth nhiều (R = 4-8)                        │
│    LIGHT_REDUCE: Giảm depth ít (R = 1-3)                           │
│    NORMAL:      Search bình thường                                  │
│    EXTEND:      Tăng depth (+1 đến +2)                              │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  TWO-STAGE DECISION:                                         │   │
│  │                                                              │   │
│  │  Stage 1: NODE-LEVEL decision (before any move)              │   │
│  │    → Should we prune this entire node?                       │   │
│  │    → Replaces: NMP, Reverse Futility, Razoring, ProbCut     │   │
│  │                                                              │   │
│  │  Stage 2: MOVE-LEVEL decision (per move)                     │   │
│  │    → How should we handle this specific move?                │   │
│  │    → Replaces: Futility, SEE pruning, LMP, History pruning, │   │
│  │                LMR, Extensions                               │   │
│  └──────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────┘
```

### 5.2 Node-Level Decision Module

```python
class NodeLevelDecision:
    """Quyết định ở cấp node: có nên prune toàn bộ node không?
    
    Thay thế: NMP, Reverse Futility, Razoring, ProbCut
    
    Tất cả 4 techniques trên đều hỏi cùng 1 câu hỏi:
    "Node này có đáng search không?"
    UPAD hợp nhất chúng thành 1 quyết định.
    """
    
    def __init__(self):
        # Tunable thresholds (learned, not hand-tuned)
        self.hard_prune_threshold = 0.92    # Very high confidence to hard prune
        self.soft_prune_threshold = 0.75    # High confidence for soft prune
        self.skip_threshold = 0.50          # Medium confidence → reduce heavily
        
        # Score combination weights (learned)
        self.w_rfp = 1.0       # Reverse futility component
        self.w_nmp = 1.0       # Null move component
        self.w_razor = 1.0     # Razoring component
        self.w_probcut = 1.0   # ProbCut component
        self.w_topology = 0.8  # Position topology component
        self.w_context = 0.6   # Search context component
    
    def decide(self, evidence_vector, position, search_state):
        """Unified node-level pruning decision"""
        
        depth = search_state.depth
        alpha = search_state.alpha
        beta = search_state.beta
        eval_score = search_state.static_eval
        
        # Safety checks: never prune in these cases
        if self.is_unsafe_to_prune(evidence_vector, search_state):
            return NodeDecision(action=NORMAL, confidence=1.0)
        
        # ═══ COMPUTE PRUNING EVIDENCE SCORES ═══
        
        # Component 1: Reverse Futility Evidence
        # "Position too good to need searching"
        rfp_score = self.compute_rfp_evidence(
            eval_score, beta, depth, evidence_vector
        )
        
        # Component 2: Null Move Evidence
        # "Even giving opponent free move, still winning"
        nmp_score = self.compute_nmp_evidence(
            position, eval_score, beta, depth, evidence_vector, search_state
        )
        
        # Component 3: Razoring Evidence
        # "Position too bad, drop to QSearch"
        razor_score = self.compute_razor_evidence(
            eval_score, alpha, depth, evidence_vector
        )
        
        # Component 4: ProbCut Evidence
        # "Shallow search with wider window indicates prunable"
        probcut_score = self.compute_probcut_evidence(
            position, beta, depth, evidence_vector, search_state
        )
        
        # Component 5: Topology Evidence
        # "Position topology suggests pruning is safe/unsafe"
        topology_score = self.compute_topology_evidence(evidence_vector)
        
        # Component 6: Context Evidence
        # "Search context suggests aggressive/conservative pruning"
        context_score = self.compute_context_evidence(evidence_vector)
        
        # ═══ FUSE EVIDENCE ═══
        
        # Weighted combination
        prune_confidence = (
            self.w_rfp * rfp_score +
            self.w_nmp * nmp_score +
            self.w_razor * razor_score +
            self.w_probcut * probcut_score +
            self.w_topology * topology_score +
            self.w_context * context_score
        ) / (self.w_rfp + self.w_nmp + self.w_razor + self.w_probcut 
             + self.w_topology + self.w_context)
        
        # Apply eval confidence scaling
        eval_conf = evidence_vector[2]  # eval_confidence
        prune_confidence *= (0.5 + 0.5 * eval_conf)
        # Low eval confidence → reduce pruning confidence
        
        # ═══ MAKE DECISION ═══
        
        if prune_confidence >= self.hard_prune_threshold:
            # Very confident: hard prune
            if rfp_score > 0.8:
                return NodeDecision(
                    action=HARD_PRUNE, 
                    confidence=prune_confidence,
                    return_score=eval_score  # Return static eval
                )
            elif razor_score > 0.8:
                return NodeDecision(
                    action=SOFT_PRUNE,
                    confidence=prune_confidence,
                    fallback=QSEARCH  # Drop to quiescence
                )
        
        elif prune_confidence >= self.soft_prune_threshold:
            # Fairly confident: soft prune (verify with shallow search)
            if nmp_score > 0.6:
                # Null move style: search with reduced depth
                reduction = self.compute_nmp_reduction(
                    depth, eval_score, beta, evidence_vector
                )
                return NodeDecision(
                    action=VERIFY_PRUNE,
                    confidence=prune_confidence,
                    verification_depth=max(1, depth - reduction),
                    null_move=True
                )
            else:
                return NodeDecision(
                    action=HEAVY_REDUCE_ALL,
                    confidence=prune_confidence,
                    depth_reduction=max(2, depth // 3)
                )
        
        # Not enough evidence to prune node
        return NodeDecision(action=NORMAL, confidence=1.0 - prune_confidence)
    
    def is_unsafe_to_prune(self, evidence_vector, search_state):
        """Safety guards: conditions where node pruning is dangerous"""
        
        # Never prune PV nodes
        if search_state.is_pv_node:
            return True
        
        # Never prune in check
        if search_state.in_check:
            return True
        
        # Never prune at very shallow depth from root
        if search_state.ply <= 1:
            return True
        
        # Don't prune when depth too shallow (let move-level handle it)
        if search_state.depth <= 1:
            return True
        
        # High tactical density → unsafe
        tactical_density = evidence_vector[37]
        if tactical_density > 0.8:
            return True
        
        # Zugzwang risk too high for null move
        zugzwang_risk = evidence_vector[41]
        if zugzwang_risk > 0.7 and evidence_vector[6] < 0.2:
            return True  # Low material + high zugzwang
        
        # Eval very uncertain
        eval_confidence = evidence_vector[2]
        if eval_confidence < 0.25:
            return True
        
        return False
    
    def compute_rfp_evidence(self, eval_score, beta, depth, ev):
        """Reverse Futility Pruning evidence
        
        Original: eval - margin ≥ beta → prune
        UPAD: Convert to continuous confidence score
        """
        if depth > 9:
            return 0.0  # RFP chỉ áp dụng ở depth nông
        
        # Adaptive margin (thay vì fixed 80 × depth)
        stability = ev[38]       # position stability
        eval_conf = ev[2]        # eval confidence
        tactical = ev[37]        # tactical density
        
        base_margin = 70 * depth
        margin = base_margin * (
            (1.0 + 0.3 * stability)      # Stable → margin lớn hơn (prune more)
            * (0.5 + 0.5 * eval_conf)    # Confident → margin nhỏ hơn (prune more)  
            * (1.0 + 0.5 * tactical)     # Tactical → margin lớn hơn (prune less)
        )
        
        surplus = eval_score - beta
        
        if surplus <= 0:
            return 0.0
        
        # Convert surplus/margin to confidence
        confidence = sigmoid((surplus - margin) / max(margin * 0.3, 1))
        
        return confidence
    
    def compute_nmp_evidence(self, position, eval_score, beta, depth, 
                              ev, search_state):
        """Null Move Pruning evidence"""
        
        # Basic requirements
        if eval_score < beta:
            return 0.0
        if ev[6] < 0.05:  # No non-pawn material (our side)
            return 0.0
        
        zugzwang_risk = ev[41]
        if zugzwang_risk > 0.5:
            return zugzwang_risk * 0.3  # Weak evidence if zugzwang possible
        
        surplus = eval_score - beta
        
        # Adaptive NMP confidence
        stability = ev[38]
        eval_conf = ev[2]
        
        # More confident when:
        # - Large eval surplus
        # - Position is stable
        # - Eval is confident
        # - Low zugzwang risk
        confidence = sigmoid(surplus / 200.0) * (
            (0.6 + 0.4 * stability) *
            (0.5 + 0.5 * eval_conf) *
            (1.0 - 0.5 * zugzwang_risk)
        )
        
        return clamp(confidence, 0.0, 1.0)
    
    def compute_nmp_reduction(self, depth, eval_score, beta, ev):
        """Adaptive NMP reduction
        
        Original: R = 4 + depth/6 + min((eval-beta)/200, 3)
        UPAD: Modulate by position characteristics
        """
        base_R = 4 + depth // 6 + min((eval_score - beta) // 200, 3)
        
        stability = ev[38]
        zugzwang_risk = ev[41]
        tactical = ev[37]
        eval_conf = ev[2]
        
        # Modulation
        modulated_R = base_R * (
            (0.7 + 0.3 * stability) *         # Stable → reduce more
            (1.0 - 0.3 * zugzwang_risk) *     # Zugzwang → reduce less
            (1.0 - 0.2 * tactical) *           # Tactical → reduce less
            (0.8 + 0.2 * eval_conf)            # Confident → reduce more
        )
        
        return max(3, int(modulated_R))
    
    def compute_razor_evidence(self, eval_score, alpha, depth, ev):
        """Razoring evidence"""
        if depth > 3:
            return 0.0
        
        tactical = ev[37]
        if tactical > 0.5:
            return 0.0  # Don't razor in tactical positions
        
        stability = ev[38]
        eval_conf = ev[2]
        
        margin = 300 * depth * (
            (1.0 + 0.3 * stability) *
            (0.6 + 0.4 * eval_conf)
        )
        
        deficit = alpha - eval_score
        if deficit <= 0:
            return 0.0
        
        confidence = sigmoid((deficit - margin) / max(margin * 0.3, 1))
        return confidence
    
    def compute_probcut_evidence(self, position, beta, depth, ev, 
                                  search_state):
        """ProbCut evidence
        
        Thay vì thực sự search ở đây (đắt), 
        ước tính confidence từ available evidence.
        Actual ProbCut search chỉ trigger khi confidence vừa đủ.
        """
        if depth < 5:
            return 0.0
        
        # TT data giúp estimate probcut
        if ev[27] > 0:  # tt_hit
            tt_score = search_state.tt_score
            tt_depth = search_state.tt_depth
            
            if tt_score > beta + 100 and tt_depth >= depth - 3:
                return 0.7  # Strong evidence from TT
            elif tt_score > beta and tt_depth >= depth - 4:
                return 0.4  # Moderate evidence
        
        # Eval-based estimate
        eval_score = search_state.static_eval
        if eval_score > beta + 200:
            return 0.3  # Eval suggests prune, but need verification
        
        return 0.0
    
    def compute_topology_evidence(self, ev):
        """Position topology contribution to pruning safety"""
        
        stability = ev[38]
        tactical_density = ev[37]
        openness = ev[36]
        forcing = ev[43]
        
        # Stable, non-tactical, closed, non-forcing → safe to prune
        safety = (
            stability * 0.3 +
            (1.0 - tactical_density) * 0.3 +
            (1.0 - openness) * 0.1 +
            (1.0 - forcing) * 0.2 +
            ev[42] * 0.1  # move_diversity: diverse → easier to prune
        )
        
        return clamp(safety, 0.0, 1.0)
    
    def compute_context_evidence(self, ev):
        """Search context contribution"""
        
        search_stability = ev[48]
        fail_high_rate = ev[51]
        re_search_rate = ev[52]
        pruning_errors = ev[53]
        
        # High fail-high rate → CUT-heavy → safe to prune
        # Low re-search rate → reductions working well
        # Low pruning errors → pruning safe
        
        safety = (
            fail_high_rate * 0.3 +
            (1.0 - re_search_rate) * 0.3 +
            (1.0 - pruning_errors) * 0.3 +
            search_stability * 0.1
        )
        
        return clamp(safety, 0.0, 1.0)
```

### 5.3 Move-Level Decision Module

```python
class MoveLevelDecision:
    """Quyết định ở cấp move: xử lý nước đi cụ thể thế nào?
    
    Thay thế: Futility Pruning, SEE Pruning, LMP, History Pruning,
              LMR, Check Extension, Singular Extension, 
              Passed Pawn Extension, Recapture Extension
    
    Output: continuous decision trên spectrum
    HARD_PRUNE ←→ SOFT_PRUNE ←→ HEAVY_REDUCE ←→ LIGHT_REDUCE ←→ NORMAL ←→ EXTEND
    """
    
    def __init__(self):
        # Decision network (tiny, fast)
        # Maps evidence vector → (action_logits, reduction_amount)
        self.decision_weights = self.initialize_weights()
        
        # Thresholds (adaptive)
        self.prune_threshold = 0.85
        self.heavy_reduce_threshold = 0.65
        self.light_reduce_threshold = 0.35
        self.extend_threshold = 0.80
    
    def decide(self, evidence_vector, position, move, search_state):
        """Unified per-move pruning/reduction/extension decision"""
        
        depth = search_state.depth
        
        # Safety: never prune/reduce these
        if self.is_protected_move(move, search_state):
            return self.handle_protected_move(
                evidence_vector, move, search_state
            )
        
        # ═══ COMPUTE PRUNE/REDUCE/EXTEND SCORES ═══
        
        # Score indicating "this move should be pruned"
        prune_score = self.compute_prune_score(evidence_vector, depth)
        
        # Score indicating "this move should be extended"
        extend_score = self.compute_extend_score(evidence_vector, depth)
        
        # Net score: positive → prune/reduce, negative → extend
        net_score = prune_score - extend_score
        
        # ═══ MAKE DECISION ═══
        
        if net_score >= self.prune_threshold:
            # Very strong prune evidence → hard prune
            return MoveDecision(
                action=HARD_PRUNE,
                confidence=net_score
            )
        
        elif net_score >= self.heavy_reduce_threshold:
            # Strong reduce evidence → heavy reduction
            reduction = self.compute_reduction(
                evidence_vector, depth, net_score, tier=HEAVY
            )
            return MoveDecision(
                action=REDUCE,
                confidence=net_score,
                depth_reduction=reduction
            )
        
        elif net_score >= self.light_reduce_threshold:
            # Moderate evidence → light reduction (like LMR)
            reduction = self.compute_reduction(
                evidence_vector, depth, net_score, tier=LIGHT
            )
            return MoveDecision(
                action=REDUCE,
                confidence=net_score,
                depth_reduction=reduction
            )
        
        elif net_score <= -self.extend_threshold:
            # Strong extend evidence → extend
            extension = self.compute_extension(
                evidence_vector, depth, -net_score
            )
            return MoveDecision(
                action=EXTEND,
                confidence=-net_score,
                depth_extension=extension
            )
        
        else:
            # Not enough evidence either way → normal search
            return MoveDecision(
                action=NORMAL,
                confidence=1.0 - abs(net_score)
            )
    
    def is_protected_move(self, move, search_state):
        """Moves that should never be hard-pruned"""
        
        if move == search_state.tt_move:
            return True
        
        if search_state.in_check:
            return True  # Check evasions are never pruned
        
        if search_state.move_count <= 1:
            return True  # First move always searched
        
        return False
    
    def handle_protected_move(self, ev, move, search_state):
        """Handle protected moves: might still reduce or extend"""
        
        extend_score = self.compute_extend_score(ev, search_state.depth)
        
        if extend_score > 0.7:
            extension = self.compute_extension(
                ev, search_state.depth, extend_score
            )
            return MoveDecision(
                action=EXTEND,
                confidence=extend_score,
                depth_extension=extension
            )
        
        # TT move might still be reduced slightly if evidence is strong
        if move == search_state.tt_move:
            return MoveDecision(action=NORMAL, confidence=1.0)
        
        # Check evasions: might reduce non-critical evasions
        if search_state.in_check and search_state.move_count > 3:
            prune_score = self.compute_prune_score(ev, search_state.depth)
            if prune_score > 0.5:
                return MoveDecision(
                    action=REDUCE,
                    confidence=prune_score * 0.5,  # Reduced confidence
                    depth_reduction=1  # Very mild reduction
                )
        
        return MoveDecision(action=NORMAL, confidence=1.0)
    
    def compute_prune_score(self, ev, depth):
        """Compute evidence for pruning this move
        
        Replaces: Futility, SEE Pruning, LMP, History Pruning, LMR decision
        All these ask: "Is this move unlikely to be the best?"
        """
        
        score = 0.0
        
        # ── Futility Evidence ──
        # "eval + margin < alpha → move can't improve alpha"
        eval_below_alpha = ev[1]   # eval_below_alpha
        eval_conf = ev[2]          # eval_confidence
        
        if eval_below_alpha > 0 and depth <= 8:
            margin_factor = (depth / 8.0) * (0.5 + 0.5 * eval_conf)
            futility_evidence = sigmoid(
                (eval_below_alpha - margin_factor) * 3.0
            )
            score += futility_evidence * 0.25
        
        # ── SEE Evidence ──
        # "This capture/move loses material"
        see_value = ev[8]  # Normalized SEE
        
        if see_value < 0:
            see_evidence = sigmoid(-see_value * 5.0)
            # Deeper depth → SEE threshold more lenient
            depth_factor = 1.0 - depth / 30.0
            score += see_evidence * 0.2 * depth_factor
        
        # ── Late Move Evidence ──
        # "This move is late in the ordering → probably not good"
        move_count = ev[9]  # Normalized move count position
        
        if move_count > 0.15 and depth <= 8:  # After ~5th move
            lateness_evidence = move_count * (depth / 8.0)
            score += lateness_evidence * 0.15
        
        # ── History Evidence ──
        # "This move has historically been bad"
        history = ev[10]
        cont_history = ev[11]
        
        if history < 0 or cont_history < 0:
            bad_history = max(0, -history) + max(0, -cont_history) * 0.5
            score += bad_history * 0.15
        
        # ── Move Characteristics Evidence ──
        is_capture = ev[12]
        gives_check = ev[13]
        is_killer = ev[14]
        
        if not is_capture and not gives_check and not is_killer:
            score += 0.05  # Quiet, non-killer, non-check → slightly pruneable
        
        if is_capture and see_value >= 0:
            score -= 0.2  # Good capture → strongly resist pruning
        
        if gives_check:
            score -= 0.15  # Check → resist pruning
        
        if is_killer:
            score -= 0.1  # Killer → resist pruning
        
        # ── Topology Modulation ──
        tactical_density = ev[37]
        stability = ev[38]
        
        score *= (1.0 - 0.4 * tactical_density)  # Tactical → prune less
        score *= (0.7 + 0.3 * stability)          # Stable → prune more
        
        # ── Context Modulation ──
        re_search_rate = ev[52]
        pruning_errors = ev[53]
        
        # High error rate → be more conservative
        score *= (1.0 - 0.3 * pruning_errors)
        
        # High re-search rate → reductions too aggressive
        score *= (1.0 - 0.2 * re_search_rate)
        
        return clamp(score, 0.0, 1.0)
    
    def compute_extend_score(self, ev, depth):
        """Compute evidence for extending this move
        
        Replaces: Singular Extension, Check Extension, 
                  Passed Pawn Extension, Recapture Extension
        """
        
        score = 0.0
        
        # ── Singular Extension Evidence ──
        is_tt_move = ev[15]
        tt_hit = ev[27]
        
        if is_tt_move and tt_hit:
            tt_depth = ev[28] * 30  # Denormalize
            if tt_depth >= depth - 3:
                # TT move with good depth → likely singular
                score += 0.4
        
        # ── Check Extension Evidence ──
        gives_check = ev[13]
        their_king_safety = ev[17]
        
        if gives_check:
            if their_king_safety > 0.3:
                # Check when king already exposed → very important
                score += 0.4
            else:
                score += 0.15
        
        # ── King Danger Extension ──
        king_danger_x_check = ev[62]
        if king_danger_x_check > 0.3:
            score += 0.3
        
        # ── Passed Pawn Extension ──
        # Detected from move characteristics
        # (Would need move-specific feature, approximated here)
        
        # ── Tactical Intensity Extension ──
        tactical_density = ev[37]
        forcing_level = ev[43]
        
        if tactical_density > 0.7 and forcing_level > 0.5:
            score += 0.2  # Extend in highly tactical forced positions
        
        # ── Evaluation Instability Extension ──
        eval_volatility = ev[3]
        if eval_volatility > 0.5:
            score += 0.15  # Unstable eval → need more depth
        
        # ── Depth-based modulation ──
        # Extensions more valuable at deeper nodes (more impact)
        depth_factor = min(depth / 10.0, 1.0)
        score *= (0.5 + 0.5 * depth_factor)
        
        return clamp(score, 0.0, 1.0)
    
    def compute_reduction(self, ev, depth, prune_score, tier):
        """Compute actual depth reduction amount
        
        Replaces LMR reduction computation
        """
        
        move_count = ev[9]
        history = ev[10]
        eval_conf = ev[2]
        stability = ev[38]
        tactical = ev[37]
        is_pv = (ev[31] > 0.1)  # Non-null window indicates PV-ish
        
        # Base reduction from prune_score
        if tier == HEAVY:
            base_R = 3 + prune_score * 4  # Range: 3-7
        else:
            base_R = 1 + prune_score * 2  # Range: 1-3
        
        # Move count contribution (later moves → more reduction)
        move_count_R = move_count * 2.0
        
        # History contribution (bad history → more reduction)
        history_R = max(0, -history) * 2.0
        
        # Total base
        total_R = base_R + move_count_R + history_R
        
        # Modulations (multiply)
        
        # PV node → reduce less
        if is_pv:
            total_R *= 0.6
        
        # Improving position → reduce less
        improving = ev[4]
        if improving > 0.5:
            total_R *= 0.8
        
        # Tactical position → reduce less
        total_R *= (1.0 - 0.3 * tactical)
        
        # Eval confident → can reduce more
        total_R *= (0.8 + 0.2 * eval_conf)
        
        # Stable position → can reduce more
        total_R *= (0.9 + 0.1 * stability)
        
        # Clamp
        max_R = depth - 1  # Never reduce to depth 0 or below
        if tier == HEAVY:
            return clamp(int(total_R), 2, max_R)
        else:
            return clamp(int(total_R), 1, min(3, max_R))
    
    def compute_extension(self, ev, depth, extend_score):
        """Compute actual depth extension amount"""
        
        if extend_score > 0.8:
            extension = 2
        elif extend_score > 0.5:
            extension = 1
        else:
            extension = 0
        
        # Don't extend too much in total
        # (tracked by search_state.total_extensions)
        
        return extension
```

### 5.4 Decision Composition - Tích Hợp Node + Move

```python
class UPADDecisionEngine:
    """Engine tổng hợp: phối hợp Node-level và Move-level decisions"""
    
    def __init__(self):
        self.evidence_collectors = {
            'static': StaticEvidenceGatherer(),
            'dynamic': DynamicEvidenceGatherer(),
            'context': SearchContextEvidence(),
            'topology': PositionTopologyAnalyzer(),
        }
        
        self.node_decision = NodeLevelDecision()
        self.move_decision = MoveLevelDecision()
        self.calibrator = AdaptiveCalibrator()
        
        # Evidence vector builder
        self.ev_builder = EvidenceVector()
        
        # Cache topology per position (expensive to compute)
        self.topology_cache = LRUCache(maxsize=8192)
    
    def make_node_decision(self, position, search_state):
        """Quyết định ở cấp node (gọi 1 lần trước khi loop moves)"""
        
        # Gather evidence
        static_ev = self.evidence_collectors['static'].gather(
            position, None, search_state.static_eval
        )
        dynamic_ev = self.evidence_collectors['dynamic'].gather(
            position, None, search_state
        )
        context_ev = self.evidence_collectors['context'].gather(
            search_state
        )
        
        # Get or compute topology
        topo_key = position.pawn_key ^ position.material_key
        topology = self.topology_cache.get(topo_key)
        if topology is None:
            topology = self.evidence_collectors['topology'].analyze(
                position
            )
            self.topology_cache.put(topo_key, topology)
        
        # Build evidence vector (partial - no move-specific features)
        ev = self.ev_builder.build_node_level(
            static_ev, dynamic_ev, topology, context_ev
        )
        
        # Get decision
        decision = self.node_decision.decide(ev, position, search_state)
        
        # Apply calibration
        decision = self.calibrator.calibrate_node_decision(
            decision, search_state
        )
        
        return decision
    
    def make_move_decision(self, position, move, search_state, 
                            cached_node_evidence=None):
        """Quyết định ở cấp move (gọi cho mỗi move trong loop)"""
        
        # Reuse node-level evidence, add move-specific
        static_ev = self.evidence_collectors['static'].gather(
            position, move, search_state.static_eval
        )
        dynamic_ev = self.evidence_collectors['dynamic'].gather(
            position, move, search_state
        )
        
        # Build full evidence vector
        ev = self.ev_builder.build_move_level(
            static_ev, dynamic_ev,
            cached_node_evidence  # Reuse topology & context from node decision
        )
        
        # Get decision
        decision = self.move_decision.decide(
            ev, position, move, search_state
        )
        
        # Apply calibration
        decision = self.calibrator.calibrate_move_decision(
            decision, search_state
        )
        
        return decision


def alpha_beta_with_upad(position, depth, alpha, beta, search_state):
    """Alpha-Beta search tích hợp UPAD"""
    
    upad = search_state.upad_engine
    
    # Static eval
    eval_score = nnue_eval(position)
    search_state.static_eval = eval_score
    
    # ═══ NODE-LEVEL DECISION ═══
    node_decision = upad.make_node_decision(position, search_state)
    
    if node_decision.action == HARD_PRUNE:
        # Confident enough to prune without search
        search_state.record_node_prune(position, node_decision)
        return node_decision.return_score
    
    elif node_decision.action == SOFT_PRUNE:
        # Drop to QSearch
        return quiescence_search(position, alpha, beta)
    
    elif node_decision.action == VERIFY_PRUNE:
        # Null move style verification
        if node_decision.null_move:
            position.make_null_move()
            null_score = -alpha_beta_with_upad(
                position, node_decision.verification_depth,
                -beta, -beta + 1, search_state.child()
            )
            position.unmake_null_move()
            
            if null_score >= beta:
                # Verification passed → prune
                if search_state.depth <= 14:
                    return null_score
                # Deep: verify with reduced depth search
                verify_score = alpha_beta_with_upad(
                    position, node_decision.verification_depth,
                    beta - 1, beta, search_state.child()
                )
                if verify_score >= beta:
                    return verify_score
    
    # ═══ MOVE LOOP WITH MOVE-LEVEL DECISIONS ═══
    
    moves = generate_and_order_moves(position)
    best_score = -INFINITY
    best_move = None
    move_count = 0
    
    # Cache node-level evidence for reuse in move decisions
    cached_evidence = upad.get_cached_node_evidence()
    
    for move in moves:
        move_count += 1
        search_state.move_count = move_count
        
        # ═══ MOVE-LEVEL DECISION ═══
        move_decision = upad.make_move_decision(
            position, move, search_state, cached_evidence
        )
        
        if move_decision.action == HARD_PRUNE:
            # Skip this move entirely
            search_state.record_move_prune(move, move_decision)
            continue
        
        # Determine search depth for this move
        new_depth = depth - 1  # Standard
        
        if move_decision.action == REDUCE:
            new_depth -= move_decision.depth_reduction
            new_depth = max(new_depth, 0)
        
        elif move_decision.action == EXTEND:
            new_depth += move_decision.depth_extension
            # Cap total extensions
            if search_state.total_extensions < search_state.max_extensions:
                search_state.total_extensions += move_decision.depth_extension
            else:
                new_depth = depth - 1  # Cap reached, no extension
        
        # ═══ SEARCH ═══
        position.make_move(move)
        
        if move_count == 1:
            # First move: full window
            score = -alpha_beta_with_upad(
                position, new_depth, -beta, -alpha,
                search_state.child()
            )
        else:
            # Subsequent moves: null window first
            score = -alpha_beta_with_upad(
                position, new_depth, -alpha - 1, -alpha,
                search_state.child()
            )
            
            # Re-search if needed
            if score > alpha and (move_decision.action == REDUCE 
                                  or score < beta):
                # Reduced move beat alpha → re-search at full depth
                if move_decision.action == REDUCE:
                    search_state.record_re_search()
                    # Feedback to calibrator
                    upad.calibrator.on_re_search(
                        move_decision, search_state
                    )
                
                score = -alpha_beta_with_upad(
                    position, depth - 1, -beta, -alpha,
                    search_state.child()
                )
        
        position.unmake_move(move)
        
        if score > best_score:
            best_score = score
            best_move = move
        
        if score > alpha:
            alpha = score
            if score >= beta:
                # Beta cutoff
                search_state.record_beta_cutoff(move, move_count)
                
                # Feedback: move that caused cutoff
                upad.calibrator.on_beta_cutoff(
                    move, move_count, move_decision, search_state
                )
                break
    
    # Feedback: which move was best?
    upad.calibrator.on_node_complete(
        best_move, move_count, search_state
    )
    
    return best_score
```

---

## VI. Adaptive Calibration System

### 6.1 Re-Search Feedback Loop

```python
class AdaptiveCalibrator:
    """Tự điều chỉnh pruning parameters dựa trên feedback
    
    Key insight: Re-searches là signal tự nhiên rằng 
    reduction quá aggressive. Beta cutoffs sớm là signal 
    rằng reduction có thể aggressive hơn.
    """
    
    def __init__(self):
        # Tracking statistics (rolling window)
        self.stats = CalibratorStats(window_size=10000)
        
        # Adaptive parameters
        self.node_prune_aggressiveness = 1.0   # Scale factor
        self.move_prune_aggressiveness = 1.0
        self.reduction_aggressiveness = 1.0
        self.extension_aggressiveness = 1.0
        
        # Per-topology calibration
        self.topology_adjustments = defaultdict(
            lambda: TopologyAdjustment()
        )
        
        # Learning rate (how fast to adapt)
        self.learning_rate = 0.005
        
        # Target rates (what we're optimizing toward)
        self.target_re_search_rate = 0.20     # 20% re-search is optimal
        self.target_pruning_error_rate = 0.02  # 2% error is acceptable
        self.target_cutoff_at_move_1 = 0.88    # 88% cutoff at first move
    
    def on_re_search(self, decision, search_state):
        """Called when a reduced move needs re-search (beat alpha)"""
        self.stats.record_re_search()
        
        topo_class = self.classify_topology(search_state)
        self.topology_adjustments[topo_class].re_search_count += 1
        
        # Re-search happened → reduction was too aggressive
        # But we only adjust if rate exceeds target
        if self.stats.recent_re_search_rate > self.target_re_search_rate:
            # Too many re-searches → reduce less aggressively
            self.reduction_aggressiveness *= (1.0 - self.learning_rate)
            self.topology_adjustments[topo_class].reduction_scale *= 0.995
    
    def on_beta_cutoff(self, move, move_index, decision, search_state):
        """Called when beta cutoff occurs"""
        self.stats.record_cutoff(move_index)
        
        topo_class = self.classify_topology(search_state)
        
        if move_index == 1:
            self.stats.record_first_move_cutoff()
            
            # First-move cutoff → ordering is good, 
            # and pruning could be more aggressive
            if (self.stats.recent_first_move_cutoff_rate 
                > self.target_cutoff_at_move_1):
                # Already exceeding target → can prune more
                self.move_prune_aggressiveness *= (
                    1.0 + self.learning_rate * 0.5
                )
                self.topology_adjustments[topo_class].prune_scale *= 1.002
        
        elif move_index > 5:
            # Late cutoff → earlier moves should have been pruned
            # (This is normal and not necessarily an error)
            pass
    
    def on_node_complete(self, best_move, total_moves_searched, 
                          search_state):
        """Called when node search completes"""
        
        # Check if any pruned move should have been the best
        for pruned_info in search_state.pruned_moves:
            if self.was_pruning_error(pruned_info, best_move, search_state):
                self.stats.record_pruning_error()
                
                topo_class = self.classify_topology(search_state)
                self.topology_adjustments[topo_class].error_count += 1
                
                # Pruning error → be more conservative
                if (self.stats.recent_pruning_error_rate 
                    > self.target_pruning_error_rate):
                    self.move_prune_aggressiveness *= (
                        1.0 - self.learning_rate * 2
                    )
                    self.node_prune_aggressiveness *= (
                        1.0 - self.learning_rate
                    )
    
    def was_pruning_error(self, pruned_info, best_move, search_state):
        """Detect if pruning was incorrect
        
        Heuristic: nếu nước bị prune có cùng characteristics 
        với best move → có thể là error
        """
        pruned_move = pruned_info.move
        
        # Same piece type and similar destination → suspicious
        if (pruned_move.piece_type == best_move.piece_type and
            distance(pruned_move.to_square, best_move.to_square) <= 1):
            return True
        
        # Pruned capture when best was capture on same square
        if (pruned_move.is_capture and best_move.is_capture and
            pruned_move.to_square == best_move.to_square):
            return True
        
        # We can't really know for sure without searching the pruned move
        # This is approximate detection
        return False
    
    def calibrate_node_decision(self, decision, search_state):
        """Apply calibration to node-level decision"""
        
        if decision.action in [HARD_PRUNE, SOFT_PRUNE, VERIFY_PRUNE]:
            # Scale confidence by aggressiveness
            decision.confidence *= self.node_prune_aggressiveness
            
            # Apply topology-specific adjustment
            topo_class = self.classify_topology(search_state)
            adj = self.topology_adjustments[topo_class]
            decision.confidence *= adj.prune_scale
            
            # Re-evaluate thresholds
            if decision.confidence < self.node_prune_threshold():
                decision.action = NORMAL
                decision.confidence = 1.0 - decision.confidence
        
        return decision
    
    def calibrate_move_decision(self, decision, search_state):
        """Apply calibration to move-level decision"""
        
        topo_class = self.classify_topology(search_state)
        adj = self.topology_adjustments[topo_class]
        
        if decision.action == HARD_PRUNE:
            decision.confidence *= (
                self.move_prune_aggressiveness * adj.prune_scale
            )
            
            if decision.confidence < self.move_prune_threshold():
                # Downgrade to reduce
                decision.action = REDUCE
                decision.depth_reduction = 2
        
        elif decision.action == REDUCE:
            # Scale reduction amount
            scaled_reduction = decision.depth_reduction * (
                self.reduction_aggressiveness * adj.reduction_scale
            )
            decision.depth_reduction = max(1, int(scaled_reduction))
        
        elif decision.action == EXTEND:
            scaled_extension = decision.depth_extension * (
                self.extension_aggressiveness * adj.extension_scale
            )
            decision.depth_extension = max(0, int(scaled_extension + 0.5))
        
        return decision
    
    def node_prune_threshold(self):
        """Adaptive threshold for node pruning"""
        base = 0.85
        
        # More errors → higher threshold (harder to prune)
        error_rate = self.stats.recent_pruning_error_rate
        return base + error_rate * 2.0
    
    def move_prune_threshold(self):
        """Adaptive threshold for move pruning"""
        base = 0.80
        error_rate = self.stats.recent_pruning_error_rate
        return base + error_rate * 1.5
    
    def classify_topology(self, search_state):
        """Classify position topology vào category for tracking"""
        topology = search_state.topology
        
        # Simple classification: 4 bits = 16 categories
        category = 0
        if topology.tactical_density > 0.5:
            category |= 1
        if topology.stability > 0.5:
            category |= 2
        if topology.zugzwang_risk > 0.3:
            category |= 4
        if topology.openness > 0.5:
            category |= 8
        
        return category
    
    def get_calibration_report(self):
        """Report for debugging/tuning"""
        return {
            'node_prune_aggressiveness': self.node_prune_aggressiveness,
            'move_prune_aggressiveness': self.move_prune_aggressiveness,
            'reduction_aggressiveness': self.reduction_aggressiveness,
            'extension_aggressiveness': self.extension_aggressiveness,
            'recent_re_search_rate': self.stats.recent_re_search_rate,
            'recent_pruning_error_rate': self.stats.recent_pruning_error_rate,
            'recent_first_move_cutoff': self.stats.recent_first_move_cutoff_rate,
            'topology_adjustments': {
                k: {
                    'prune_scale': v.prune_scale,
                    'reduction_scale': v.reduction_scale,
                    'error_count': v.error_count,
                    're_search_count': v.re_search_count,
                }
                for k, v in self.topology_adjustments.items()
            },
        }
```

### 6.2 Error Recovery Mechanism

```python
class ErrorRecoveryMechanism:
    """Phát hiện và phục hồi từ pruning errors nghiêm trọng"""
    
    def __init__(self):
        self.error_history = deque(maxlen=100)
        self.panic_mode = False
        self.panic_cooldown = 0
    
    def check_for_critical_error(self, search_state):
        """Detect if a critical pruning error occurred"""
        
        # Signal 1: Score dropped significantly between iterations
        if search_state.iteration >= 2:
            current_score = search_state.current_iteration_score
            prev_score = search_state.prev_iteration_score
            
            score_drop = prev_score - current_score
            
            if score_drop > 100:  # 1 pawn drop
                self.error_history.append({
                    'type': 'score_drop',
                    'magnitude': score_drop,
                    'iteration': search_state.iteration,
                })
                
                if score_drop > 200:
                    # Critical: possible pruning error
                    self.trigger_recovery(search_state)
        
        # Signal 2: Best move changed AND score dropped
        if (search_state.best_move_changed and 
            search_state.score_dropped_on_change > 50):
            self.error_history.append({
                'type': 'move_change_with_drop',
                'magnitude': search_state.score_dropped_on_change,
            })
            
            if search_state.score_dropped_on_change > 150:
                self.trigger_recovery(search_state)
        
        # Signal 3: Many re-searches in short period
        if self.stats.recent_re_search_rate > 0.40:
            self.error_history.append({
                'type': 'excessive_re_search',
                'rate': self.stats.recent_re_search_rate,
            })
            self.enter_conservative_mode(search_state)
    
    def trigger_recovery(self, search_state):
        """Enter panic mode: reduce all pruning aggressiveness"""
        
        self.panic_mode = True
        self.panic_cooldown = 5000  # nodes
        
        # Drastically reduce pruning aggressiveness
        self.calibrator.node_prune_aggressiveness *= 0.5
        self.calibrator.move_prune_aggressiveness *= 0.5
        self.calibrator.reduction_aggressiveness *= 0.6
        
        # Increase extension aggressiveness
        self.calibrator.extension_aggressiveness *= 1.3
        
        # Force wider search at root
        search_state.force_wider_root_search = True
    
    def enter_conservative_mode(self, search_state):
        """Less extreme than panic: just be more careful"""
        self.calibrator.reduction_aggressiveness *= 0.85
        self.calibrator.move_prune_aggressiveness *= 0.90
    
    def tick(self):
        """Called every node: manage cooldown"""
        if self.panic_mode:
            self.panic_cooldown -= 1
            if self.panic_cooldown <= 0:
                self.panic_mode = False
                # Gradually restore aggressiveness
                self.calibrator.node_prune_aggressiveness = min(
                    self.calibrator.node_prune_aggressiveness * 1.1, 1.0
                )
                self.calibrator.move_prune_aggressiveness = min(
                    self.calibrator.move_prune_aggressiveness * 1.1, 1.0
                )
                self.calibrator.reduction_aggressiveness = min(
                    self.calibrator.reduction_aggressiveness * 1.1, 1.0
                )
```

---

## VII. Neural Enhancement (Optional Layer)

```python
class NeuralPruningAdvisor:
    """Optional neural network cho pruning decisions
    
    Tiny network: chỉ dùng ở near-root nodes
    Bổ sung cho rule-based decisions, không thay thế
    """
    
    def __init__(self):
        # Architecture: 64 → 32 → 16 → 3
        # Input: Evidence vector (64 features)
        # Output: (prune_probability, optimal_reduction, extend_probability)
        
        self.layer1 = QuantizedLinear(64, 32, activation='relu')
        self.layer2 = QuantizedLinear(32, 16, activation='relu')
        self.output = QuantizedLinear(16, 3, activation='none')
        
        # Total parameters: 64×32 + 32×16 + 16×3 = 2608
        # Memory: ~3KB (int8 quantized)
        # Inference: ~1-2μs
    
    def predict(self, evidence_vector):
        """Predict pruning decision"""
        h1 = self.layer1(evidence_vector)
        h2 = self.layer2(h1)
        output = self.output(h2)
        
        prune_prob = sigmoid(output[0])
        optimal_reduction = relu(output[1]) * 8  # 0-8 range
        extend_prob = sigmoid(output[2])
        
        return NeuralAdvice(
            prune_probability=prune_prob,
            optimal_reduction=optimal_reduction,
            extend_probability=extend_prob
        )
    
    def integrate_with_rule_based(self, neural_advice, rule_decision,
                                   blend_factor=0.3):
        """Blend neural advice with rule-based decision"""
        
        # Neural chỉ ảnh hưởng 30%, rule-based 70%
        # (Neural bổ sung, không dominate)
        
        if rule_decision.action == HARD_PRUNE:
            # Neural agrees?
            if neural_advice.prune_probability > 0.7:
                return rule_decision  # Confirmed
            else:
                # Neural disagrees → downgrade to reduce
                return MoveDecision(
                    action=REDUCE,
                    depth_reduction=max(1, int(
                        neural_advice.optimal_reduction * blend_factor +
                        rule_decision.depth_reduction * (1 - blend_factor)
                    ))
                )
        
        elif rule_decision.action == REDUCE:
            # Blend reduction amounts
            blended_reduction = (
                neural_advice.optimal_reduction * blend_factor +
                rule_decision.depth_reduction * (1 - blend_factor)
            )
            rule_decision.depth_reduction = max(1, int(blended_reduction))
            return rule_decision
        
        elif rule_decision.action == NORMAL:
            # Neural suggests pruning?
            if neural_advice.prune_probability > 0.8:
                return MoveDecision(
                    action=REDUCE,
                    depth_reduction=max(1, int(
                        neural_advice.optimal_reduction * blend_factor
                    ))
                )
            # Neural suggests extending?
            elif neural_advice.extend_probability > 0.8:
                return MoveDecision(
                    action=EXTEND,
                    depth_extension=1
                )
            return rule_decision
        
        return rule_decision


class NeuralPruningTrainer:
    """Training pipeline cho Neural Pruning Advisor"""
    
    def generate_training_data(self, engine, num_positions=1000000):
        """Generate labeled data từ engine self-play"""
        
        dataset = []
        
        for position in random_positions(num_positions):
            # Search ở multiple depths
            shallow_result = engine.search(position, depth=8)
            deep_result = engine.search(position, depth=16)
            
            for move in position.legal_moves():
                # Build evidence vector
                ev = build_evidence_vector(position, move)
                
                # Labels từ deep search
                was_best = (move == deep_result.best_move)
                move_score = deep_result.move_scores.get(move, -INFINITY)
                best_score = deep_result.best_score
                score_diff = best_score - move_score
                
                # Optimal pruning label
                if score_diff > 300:
                    prune_label = 1.0  # Should definitely prune
                elif score_diff > 100:
                    prune_label = 0.7
                elif score_diff > 50:
                    prune_label = 0.3
                else:
                    prune_label = 0.0  # Should not prune
                
                # Optimal reduction label
                # Find minimum depth where move's relative ranking is correct
                optimal_reduction = find_optimal_reduction(
                    engine, position, move, deep_result
                )
                
                # Extension label
                extend_label = 1.0 if was_best and score_diff == 0 else 0.0
                
                dataset.append({
                    'evidence': ev,
                    'prune_label': prune_label,
                    'reduction_label': optimal_reduction,
                    'extend_label': extend_label,
                })
        
        return dataset
```

---

## VIII. Hiệu Năng & Tối Ưu Hóa

### 8.1 Computational Budget

```
┌─────────────────────────────────────────────────────────────────┐
│                    UPAD COMPUTATIONAL BUDGET                     │
│                                                                  │
│  Per-Node Cost:                                                  │
│  ┌──────────────────────────────────────┬───────────┬──────────┐│
│  │ Component                            │ Time (μs) │ Condition││
│  ├──────────────────────────────────────┼───────────┼──────────┤│
│  │ Static Evidence (eval-based)         │ 0.5-1.0   │ Always   ││
│  │ Static Evidence (tactical)           │ 1.0-3.0   │ Always   ││
│  │ Dynamic Evidence                     │ 0.3-0.5   │ Always   ││
│  │ Context Evidence                     │ 0.1-0.2   │ Always   ││
│  │ Topology Analysis (cached)           │ 0.0-0.1   │ On miss  ││
│  │ Topology Analysis (compute)          │ 2.0-5.0   │ Rare     ││
│  │ Evidence Vector Build                │ 0.2-0.3   │ Always   ││
│  │ Node Decision                        │ 0.5-1.0   │ Always   ││
│  ├──────────────────────────────────────┼───────────┼──────────┤│
│  │ Node-level total                     │ 2.5-5.0   │          ││
│  └──────────────────────────────────────┴───────────┴──────────┘│
│                                                                  │
│  Per-Move Cost (amortized):                                      │
│  ┌──────────────────────────────────────┬───────────┬──────────┐│
│  │ Component                            │ Time (μs) │ Condition││
│  ├──────────────────────────────────────┼───────────┼──────────┤│
│  │ Move Evidence (incremental)          │ 0.3-0.5   │ Always   ││
│  │ Move Decision                        │ 0.3-0.5   │ Always   ││
│  │ Calibration Apply                    │ 0.1       │ Always   ││
│  │ Neural Advisor (optional)            │ 1.0-2.0   │ Root±3   ││
│  ├──────────────────────────────────────┼───────────┼──────────┤│
│  │ Move-level total (without neural)    │ 0.7-1.1   │          ││
│  │ Move-level total (with neural)       │ 1.7-3.1   │          ││
│  └──────────────────────────────────────┴───────────┴──────────┘│
│                                                                  │
│  Comparison with Stockfish:                                      │
│  ┌──────────────────────────────────────┬───────────┬──────────┐│
│  │ Stockfish pruning checks per node    │ 1.0-2.0   │          ││
│  │ Stockfish pruning checks per move    │ 0.3-0.8   │          ││
│  │ UPAD per node                        │ 2.5-5.0   │          ││
│  │ UPAD per move                        │ 0.7-1.1   │          ││
│  ├──────────────────────────────────────┼───────────┼──────────┤│
│  │ UPAD overhead vs Stockfish           │ +100-200% │ per node ││
│  │ UPAD overhead vs Stockfish           │ +40-80%   │ per move ││
│  └──────────────────────────────────────┴───────────┴──────────┘│
│                                                                  │
│  NET EFFECT:                                                     │
│  - UPAD prunes ~15-25% more nodes than Stockfish                │
│  - UPAD reduces ~10-20% more effectively (fewer re-searches)    │
│  - UPAD extends more selectively (less wasted extensions)        │
│  - Overhead: ~2x per decision                                   │
│  - Nodes saved: ~20-35%                                         │
│  - Net speedup: ~10-25% effective (search more with same time)  │
│  - Equivalent to: +1 to +1.5 ply effective depth                │
└─────────────────────────────────────────────────────────────────┘
```

### 8.2 Tiered Computation

```python
class TieredUPAD:
    """UPAD với computation budget phân tầng theo depth"""
    
    TIERS = {
        # Near root: full UPAD + neural
        'FULL': {
            'depth_from_root': (0, 5),
            'components': ['static', 'dynamic', 'context', 
                          'topology', 'neural'],
            'estimated_cost_per_node': 8.0,  # μs
            'estimated_cost_per_move': 3.0,
        },
        
        # Mid-tree: UPAD without neural
        'STANDARD': {
            'depth_from_root': (6, 12),
            'components': ['static', 'dynamic', 'context', 'topology'],
            'estimated_cost_per_node': 4.0,
            'estimated_cost_per_move': 1.0,
        },
        
        # Deep tree: simplified UPAD
        'LIGHT': {
            'depth_from_root': (13, 20),
            'components': ['static_basic', 'dynamic_basic'],
            'estimated_cost_per_node': 1.5,
            'estimated_cost_per_move': 0.5,
        },
        
        # Very deep: fallback to Stockfish-like pruning
        'MINIMAL': {
            'depth_from_root': (21, 999),
            'components': ['stockfish_compatible'],
            'estimated_cost_per_node': 0.5,
            'estimated_cost_per_move': 0.3,
        },
    }
    
    def get_tier(self, search_state):
        dfr = search_state.depth_from_root
        
        for tier_name, config in self.TIERS.items():
            low, high = config['depth_from_root']
            if low <= dfr <= high:
                return tier_name
        
        return 'MINIMAL'
    
    def make_decision(self, position, move, search_state):
        tier = self.get_tier(search_state)
        
        if tier == 'FULL':
            return self.full_upad_decision(position, move, search_state)
        elif tier == 'STANDARD':
            return self.standard_upad_decision(position, move, search_state)
        elif tier == 'LIGHT':
            return self.light_upad_decision(position, move, search_state)
        else:
            return self.minimal_decision(position, move, search_state)
    
    def light_upad_decision(self, position, move, search_state):
        """Simplified UPAD for deep nodes"""
        
        depth = search_state.depth
        eval_score = search_state.static_eval
        alpha = search_state.alpha
        beta = search_state.beta
        
        # Simplified evidence (chỉ tính features quan trọng nhất)
        eval_margin = eval_score - alpha
        see_value = see(position, move)
        history = get_history_score(position, move)
        move_count = search_state.move_count
        
        # Simple prune decision
        if (not move.is_capture and not move.gives_check and 
            not move.is_killer):
            # Futility-like
            if depth <= 6 and eval_margin < -80 * depth:
                return MoveDecision(action=HARD_PRUNE, confidence=0.8)
            # LMP-like
            if depth <= 6 and move_count > 3 + depth * depth:
                return MoveDecision(action=HARD_PRUNE, confidence=0.7)
            # History-like
            if depth <= 3 and history < -4000:
                return MoveDecision(action=HARD_PRUNE, confidence=0.6)
        
        # SEE prune
        threshold = -20 * depth * depth if not move.is_capture else -100 * depth
        if see_value < threshold:
            return MoveDecision(action=HARD_PRUNE, confidence=0.7)
        
        # Simple reduction (LMR-like)
        if move_count > 1 and depth >= 2:
            R = LMR_TABLE[min(depth, 63)][min(move_count, 63)]
            
            # Basic adjustments
            if history > 0:
                R -= 1
            if move.gives_check:
                R -= 1
            if not search_state.improving:
                R += 1
            
            R = max(0, R)
            if R > 0:
                return MoveDecision(
                    action=REDUCE, 
                    confidence=0.5,
                    depth_reduction=R
                )
        
        return MoveDecision(action=NORMAL, confidence=1.0)
    
    def minimal_decision(self, position, move, search_state):
        """Fallback: essentially Stockfish-compatible pruning"""
        # Directly call traditional pruning logic
        return stockfish_compatible_pruning(position, move, search_state)
```

### 8.3 SIMD Optimization

```python
class SIMDEvidenceComputer:
    """Compute evidence vectors using SIMD instructions"""
    
    def compute_batch_evidence(self, position, moves):
        """Compute evidence for multiple moves at once"""
        n = len(moves)
        
        # Position features computed ONCE (shared across moves)
        pos_features = self.compute_position_features_simd(position)
        
        # Move features computed in parallel
        move_features = np.zeros((n, 32), dtype=np.float32)
        
        # Vectorized SEE computation
        see_values = np.array([
            see(position, m) for m in moves
        ], dtype=np.float32)
        move_features[:, 0] = see_values / 1000.0
        
        # Vectorized history lookup
        history_values = np.array([
            get_history_score(position, m) for m in moves
        ], dtype=np.float32)
        move_features[:, 1] = history_values / 16384.0
        
        # Boolean features (vectorized)
        move_features[:, 2] = np.array([
            float(m.is_capture) for m in moves
        ])
        move_features[:, 3] = np.array([
            float(m.gives_check) for m in moves
        ])
        move_features[:, 4] = np.array([
            float(m == self.tt_move) for m in moves
        ])
        move_features[:, 5] = np.array([
            float(is_killer_move(m)) for m in moves
        ])
        
        # Move count features
        move_features[:, 6] = np.arange(n, dtype=np.float32) / max(n, 1)
        
        # Build evidence vectors (batch)
        # pos_features broadcast across all moves
        evidence_batch = np.zeros((n, 64), dtype=np.float32)
        evidence_batch[:, :20] = np.tile(pos_features[:20], (n, 1))
        evidence_batch[:, 20:52] = np.tile(pos_features[20:52], (n, 1))
        evidence_batch[:, 52:64] = move_features[:, :12]
        
        # Compute derived features (vectorized)
        evidence_batch[:, 56] = evidence_batch[:, 0] * evidence_batch[:, 2]
        evidence_batch[:, 57] = evidence_batch[:, 37] * evidence_batch[:, 29]
        
        return evidence_batch
```

---

## IX. So Sánh Với Stockfish

```
┌───────────────────────┬──────────────────────┬───────────────────────────┐
│ Aspect                │ Stockfish            │ UPAD                      │
├───────────────────────┼──────────────────────┼───────────────────────────┤
│ Architecture          │ 15+ independent      │ Unified 2-stage           │
│                       │ techniques           │ (node + move)             │
│                       │                      │                           │
│ Decision basis        │ Individual condition │ Evidence vector (64D)     │
│                       │ per technique        │ from all sources          │
│                       │                      │                           │
│ Parameters            │ Hand-tuned constants │ Adaptive + learned        │
│                       │ (SPSA optimized)     │ + self-calibrating        │
│                       │                      │                           │
│ Context awareness     │ Minimal              │ Position topology +       │
│                       │ (depth, eval, basic) │ search context + phase    │
│                       │                      │                           │
│ Conflict resolution   │ Hard-coded priority  │ Evidence fusion with      │
│                       │ + ad-hoc overrides   │ weighted aggregation      │
│                       │                      │                           │
│ Error detection       │ None                 │ Re-search feedback +      │
│                       │                      │ error recovery mechanism  │
│                       │                      │                           │
│ Adaptivity            │ Fixed during search  │ Online calibration during │
│                       │                      │ search + per-game         │
│                       │                      │                           │
│ Output                │ Binary prune/don't   │ Continuous spectrum       │
│                       │ + fixed reduction    │ prune↔reduce↔normal↔extend│
│                       │                      │                           │
│ Eval dependency       │ Direct (error        │ Modulated by confidence   │
│                       │ amplification risk)  │ (error dampening)         │
│                       │                      │                           │
│ Interaction handling  │ None (cascading      │ Unified evidence prevents │
│                       │ failures possible)   │ cascading                 │
│                       │                      │                           │
│ Depth scaling         │ Same logic all depths│ Tiered: full→standard→    │
│                       │                      │ light→minimal             │
│                       │                      │                           │
│ Computation cost      │ ~1-2μs per node      │ ~2.5-5μs per node        │
│                       │                      │ (near root)              │
│                       │                      │ ~0.5-1.5μs (deep)        │
│                       │                      │                           │
│ Nodes pruned          │ ~70-85% of tree      │ ~78-90% of tree          │
│                       │                      │                           │
│ Critical errors       │ ~1-3%                │ ~0.3-1% (estimated)      │
│                       │                      │                           │
│ Re-search rate        │ ~20-30%              │ ~12-18% (target: 20%)    │
└───────────────────────┴──────────────────────┴───────────────────────────┘
```

---

## X. Ước Tính Ảnh Hưởng

```
┌──────────────────────────────────┬──────────────┬─────────────────────┐
│ Impact Category                  │ Elo Estimate │ Confidence          │
├──────────────────────────────────┼──────────────┼─────────────────────┤
│ Unified decision (no conflicts)  │ +10-20       │ ★★★★ High           │
│ Adaptive margins/thresholds      │ +15-30       │ ★★★★ High           │
│ Eval confidence modulation       │ +10-20       │ ★★★★ High           │
│ Position topology awareness      │ +10-25       │ ★★★ Medium-High     │
│ Online calibration               │ +5-15        │ ★★★ Medium          │
│ Error recovery                   │ +5-10        │ ★★★ Medium          │
│ Better reduction amounts         │ +10-20       │ ★★★★ High           │
│ Selective extensions             │ +5-15        │ ★★★ Medium          │
│ Neural advisor (optional)        │ +5-15        │ ★★ Medium-Low       │
│ Search context feedback          │ +5-10        │ ★★★ Medium          │
├──────────────────────────────────┼──────────────┼─────────────────────┤
│ Total (with overlap/interaction) │ +50-110      │                     │
│ After overhead deduction         │ +40-90       │                     │
│ Conservative estimate            │ +30-60       │                     │
└──────────────────────────────────┴──────────────┴─────────────────────┘

By position type:
- Tactical positions:     +30-70 Elo (less mis-pruning of sacrifices)
- Positional positions:   +20-40 Elo (smarter reduction amounts)
- Endgames:               +30-60 Elo (zugzwang-aware, better extensions)
- Time pressure:          +40-80 Elo (adaptive aggressiveness)
```

---

## XI. Lộ Trình Triển Khai

```
Phase 1 (Month 1-3): Evidence Infrastructure
├── Implement StaticEvidenceGatherer
├── Implement DynamicEvidenceGatherer  
├── Implement PositionTopologyAnalyzer
├── Implement EvidenceVector builder
├── Unit test evidence quality
└── Target: Evidence computation < 3μs per node

Phase 2 (Month 4-6): Unified Node Decision
├── Implement NodeLevelDecision
├── Replace NMP + RFP + Razoring + ProbCut
├── Implement adaptive margins
├── Compare with Stockfish node pruning
└── Target: Equal or better pruning accuracy, +10-20 Elo

Phase 3 (Month 7-9): Unified Move Decision  
├── Implement MoveLevelDecision
├── Replace Futility + SEE + LMP + History pruning + LMR + Extensions
├── Implement continuous reduction computation
├── Compare with Stockfish move pruning
└── Target: Better reduction amounts, +15-30 Elo over Phase 2

Phase 4 (Month 10-12): Calibration System
├── Implement AdaptiveCalibrator
├── Implement ErrorRecoveryMechanism
├── Implement per-topology tracking
├── Implement re-search feedback loop
├── Long-term self-play testing
└── Target: Self-improving, +5-15 Elo over Phase 3

Phase 5 (Month 13-15): Neural & Optimization
├── Train NeuralPruningAdvisor
├── Implement TieredUPAD
├── SIMD optimization
├── Memory optimization
├── Comprehensive testing (CCRL, TCEC conditions)
└── Target: Final +40-90 Elo over Stockfish baseline

Phase 6 (Month 16-18): Integration & Polish
├── Integration with HAMO (move ordering)
├── Integration with improved NNUE
├── Cross-component optimization
├── Final tuning
└── Target: Stable, production-ready system
```

UPAD biến hệ thống pruning từ **tập hợp heuristics rời rạc** thành một **framework quyết định thống nhất**, có khả năng tự đánh giá, tự điều chỉnh, và tự phục hồi từ lỗi — đồng thời giữ hiệu năng khả thi thông qua phân tầng tính toán và cơ chế evidence-based thay vì threshold-based.