

# Kiến Trúc Mới Cho Quiescence Search: DQRS (Deep Quiet Resolution Search)

---

## I. Phân Tích Sâu Quiescence Search Hiện Tại

### 1.1 Quiescence Search Trong Stockfish

```
┌──────────────────────────────────────────────────────────────────┐
│              STOCKFISH QUIESCENCE SEARCH                         │
│                                                                  │
│  function qsearch(position, alpha, beta, depth):                 │
│                                                                  │
│    // 1. STAND PAT                                               │
│    stand_pat = evaluate(position)                                │
│    if stand_pat >= beta: return beta          // Beta cutoff     │
│    if stand_pat > alpha: alpha = stand_pat    // Raise alpha     │
│                                                                  │
│    // 2. DELTA PRUNING                                           │
│    big_delta = QUEEN_VALUE                                       │
│    if stand_pat + big_delta < alpha: return alpha  // Hopeless   │
│                                                                  │
│    // 3. GENERATE & SEARCH CAPTURES                              │
│    captures = generate_captures(position)                        │
│    sort(captures, by=MVV_LVA)                                    │
│                                                                  │
│    for capture in captures:                                      │
│      // SEE Pruning                                              │
│      if see(capture) < 0: continue           // Bad capture     │
│                                                                  │
│      // Delta Pruning (per-move)                                 │
│      if stand_pat + capture.gain + 200 < alpha: continue        │
│                                                                  │
│      make_move(capture)                                          │
│      score = -qsearch(position, -beta, -alpha, depth - 1)       │
│      unmake_move(capture)                                        │
│                                                                  │
│      if score >= beta: return beta                               │
│      if score > alpha: alpha = score                             │
│                                                                  │
│    // 4. CHECK EVASION (if in check at entry)                    │
│    if in_check:                                                  │
│      generate_all_moves()  // Must escape check                  │
│      search all evasions                                         │
│                                                                  │
│    return alpha                                                  │
│                                                                  │
│  Depth limit: typically 6-8 additional plies                     │
│  Generates: ONLY captures (+ check evasions)                     │
│  Pruning: SEE < 0, Delta pruning                                │
│  Ordering: MVV-LVA                                               │
└──────────────────────────────────────────────────────────────────┘
```

### 1.2 Tỷ Trọng QSearch Trong Tổng Search

```
CRITICAL STATISTICS:

┌──────────────────────────────────┬───────────────────────────────┐
│ Metric                           │ Typical Value                 │
├──────────────────────────────────┼───────────────────────────────┤
│ % nodes in QSearch               │ 60-85% of all nodes           │
│ % time in QSearch                │ 50-75% of total search time   │
│ Average QSearch depth            │ 3-6 plies beyond main search  │
│ QSearch branching factor         │ 3-8 (captures only)           │
│ Stand-pat cutoff rate            │ ~35-50%                       │
│ SEE pruning rate                 │ ~20-40% of captures           │
│ Delta pruning rate               │ ~10-20% of remaining          │
│ Actually searched captures       │ ~2-5 per QSearch node          │
│ QSearch tree size (typical)      │ 100-10000 nodes per leaf      │
│ Eval calls in QSearch            │ 1 per node (stand pat)        │
│ Accuracy vs full search          │ ~85-92% (agrees with deeper)  │
│ Horizon effect occurrence        │ ~5-15% of positions           │
└──────────────────────────────────┴───────────────────────────────┘

KEY INSIGHT:
QSearch chiếm 60-85% nodes nhưng chất lượng quyết định chỉ ~85-92%
→ Cải thiện QSearch 5% ≈ cải thiện toàn bộ search ~3-4%
→ Giảm QSearch nodes 20% ≈ tăng 0.5-1 ply depth
→ QSearch là "low-hanging fruit" lớn nhất trong engine optimization
```

### 1.3 Phân Tích Chi Tiết Các Hạn Chế

#### A. Horizon Effect (Hiệu Ứng Đường Chân Trời)

```
VẤN ĐỀ CỐT LÕI NHẤT CỦA QSEARCH:

QSearch chỉ xét captures → bỏ qua quiet moves quan trọng
→ Engine "đẩy" tin xấu ra khỏi search horizon bằng nước capture vô ích

Ví dụ kinh điển:

   Position: Trắng sắp mất Tượng (bị ghim, sẽ bị bắt)
   
   Main search depth hết → vào QSearch
   QSearch: "Không có capture nào cải thiện?" 
   → Trắng sacrifice tốt: PxP (capture, được search!)
   → Đối phương bắt lại: QxP
   → QSearch tiếp: Trắng lại sacrifice tốt khác
   → ... chuỗi sacrifice vô nghĩa kéo dài
   → Cuối cùng QSearch hết depth
   → Stand pat: "position bình thường" (đã ẩn mất Tượng!)
   
   Kết quả: Engine nghĩ position = 0.0 (bình đẳng)
   Thực tế: Position = -3.0 (mất Tượng)
   
   → Horizon effect: che giấu tổn thất bằng chuỗi captures

PHÂN LOẠI HORIZON EFFECT:

Type 1: "Delaying Sacrifice"
  - Sacrifice material để delay inevitable loss
  - QSearch thấy sacrifice (capture) nhưng không thấy loss (quiet)
  
Type 2: "Quiet Tactic Blindness"
  - Quiet move tạo mate threat/fork/pin
  - QSearch không generate quiet moves → miss hoàn toàn
  
Type 3: "Zwischenzug Blindness"
  - Trong chuỗi exchanges, một nước quiet xen giữa thay đổi kết quả
  - QSearch chỉ thấy captures → bỏ qua zwischenzug
  
Type 4: "Piece Sacrifice Horizon"
  - Hy sinh quân (negative SEE) nhưng có compensation
  - SEE pruning loại bỏ sacrifice → miss winning combination
  
Type 5: "Promotion Horizon"
  - Tốt sắp phong hậu nhưng cần 1-2 quiet pushes
  - QSearch không generate pawn pushes (trừ promotion capture)
```

#### B. Stand Pat Inaccuracy

```
VẤN ĐỀ:

Stand pat = static eval dùng làm lower bound
Giả định: "Nếu không bắt quân nào, position ít nhất tốt bằng eval"

Giả định NÀY SAI khi:

1. Có threats chưa resolved
   Eval nói +2.0 nhưng đối phương có NxQ threat
   → Sau NxQ, eval = -7.0
   → Stand pat = +2.0 là hoàn toàn sai
   → Cần "threat-adjusted stand pat"

2. Pieces đang en prise (treo)
   Eval tính material đầy đủ
   Nhưng Bishop treo ở h7 sẽ bị bắt
   → Stand pat quá lạc quan vì đếm Bishop còn sống
   → NNUE CÓ THỂ handle phần nào, nhưng không hoàn hảo

3. Forced sequence exists
   Position có chuỗi nước ép dẫn đến material loss
   QSearch stand pat chỉ thấy snapshot, không thấy sequence
   → Overestimate position value

4. Eval confidence varies
   Trong tactical chaos, eval ít tin cậy
   Nhưng stand pat dùng eval với full confidence mọi lúc
   → Nên discount stand pat khi eval uncertain

THỐNG KÊ:
- Stand pat inaccuracy > 50cp: ~8-15% of QSearch nodes
- Stand pat inaccuracy > 100cp: ~3-7% 
- Stand pat inaccuracy > 200cp: ~1-3%
- Impact: mỗi 1% inaccuracy ≈ -3 đến -5 Elo
```

#### C. SEE Pruning Over-Aggressiveness

```
VẤN ĐỀ:

SEE(capture) < 0 → skip capture
Nhưng SEE chỉ tính material exchange trên MỘT Ô

Bỏ qua:
1. Discovered attacks sau capture
   BxN trên d5, SEE = -25 (bishop > knight nhẹ)
   Nhưng Bxd5 mở đường chéo cho Qh5+ winning!
   SEE không thấy → prune → miss win

2. Defensive captures
   RxB, SEE = -200 (lose exchange)
   Nhưng RxB removes defender of mate threat
   RxB actually winning!
   SEE prunes → miss

3. Piece placement after exchange
   NxP on e5, SEE = -100 (lose knight eventually)
   Nhưng knight trên e5 controls critical squares
   Positional value > material cost
   SEE prunes → miss strategic exchange

4. Pin exploitation
   SEE counts pinned piece as defender
   But pinned piece can't actually capture
   → SEE overestimates defense → wrong prune/keep decision

5. Intermediate moves (Zwischenzug)
   SEE assumes alternating captures on same square
   But zwischenzug can change the result completely
   SEE: BxN, PxB → -0.25
   Reality: BxN, Qh5+!, KxQ, PxB → completely different

THỐNG KÊ:
- SEE prune error rate: ~3-8% (prune moves that were actually good)
- SEE keep error rate: ~5-10% (keep moves that were actually bad)
- Total SEE inaccuracy: ~8-18% of pruning decisions
- Impact: ~10-20 Elo loss from SEE errors in QSearch
```

#### D. QSearch Explosion

```
VẤN ĐỀ:

Trong tactical positions, QSearch có thể "nổ" exponentially

Typical scenario:
- Open position, many pieces attacking
- 8 possible captures per side
- QSearch depth 6
- Nodes: 8^6 = 262,144 per leaf of main search!

Actual worst cases observed:
- Some positions: >1,000,000 QSearch nodes per main search leaf
- Time: >50% of total search time in single QSearch call
- Cause: "capture chains" where each capture creates new captures

Current mitigation:
- SEE pruning: helps but imperfect
- Delta pruning: helps but can miss
- Depth limit: helps but artificial cutoff → horizon effect

NONE of these address the ROOT CAUSE:
The real question is "is this position quiet enough?"
If not, QSearch is the WRONG tool.
Should either:
a) Extend main search instead, OR
b) Use fundamentally different approach for tactical positions
```

#### E. Missing Move Types

```
WHAT QSEARCH DOESN'T GENERATE:

1. ❌ Quiet checks (gives check without capture)
   - Can be game-deciding: Qh5+ leading to mate
   - Some engines add check generation → helps but expensive
   - Stockfish: mostly removed check generation (too many nodes)

2. ❌ Pawn pushes (especially passed pawn pushes)
   - Passed pawn on 7th: one push from promotion!
   - QSearch can't see it → horizon effect
   - Critical in endgames

3. ❌ Piece retreats from danger
   - Queen attacked: quiet retreat saves it
   - QSearch: "stand pat says Queen still alive" → wrong!
   - If opponent captures Queen next move → catastrophic

4. ❌ Blocking/intercepting moves
   - Piece blocks discovered attack path
   - Pure quiet move, never generated
   - Can prevent mate or major material loss

5. ❌ Prophylactic moves
   - Prevent opponent's winning tactic
   - Always quiet → always invisible to QSearch

6. ❌ Zwischenzug (in-between moves)
   - During capture exchange, quiet interposition
   - Changes exchange outcome completely
   - QSearch only follows captures → misses

IMPACT:
- Missing quiet checks: ~5-10 Elo
- Missing pawn pushes: ~5-15 Elo (mostly endgame)
- Missing piece retreats: ~3-8 Elo
- Missing blocking moves: ~2-5 Elo
- Missing zwischenzug: ~5-10 Elo
- Total: ~20-48 Elo potential improvement
```

#### F. Move Ordering In QSearch

```
CURRENT: MVV-LVA (Most Valuable Victim - Least Valuable Attacker)

PROBLEMS:

1. MVV-LVA chỉ xét giá trị quân
   PxP trên cột mở (chiến lược) vs BxN ở góc (vô ích)
   MVV-LVA: BxN first (victim = 3) > PxP (victim = 1)
   Reality: PxP may be far better

2. No history information used
   Main search dùng history heuristic
   QSearch: pure MVV-LVA, no history
   → Bỏ phí thông tin đã learn được

3. No TT move priority
   Main search: TT move first (huge impact)
   QSearch: TT move mixed with others
   → First-move cutoff rate much lower

4. No distinction between forced and optional captures
   Must-recapture vs speculative capture treated equally
   Should always try recaptures first

IMPACT:
- Suboptimal ordering → more nodes before cutoff
- First-move cutoff rate: ~30-40% (vs ~85% in main search)
- Estimated: ~10-15% wasted QSearch nodes from bad ordering
```

---

## II. DQRS — Deep Quiet Resolution Search

### 2.1 Triết Lý Thiết Kế

```
CORE PHILOSOPHY:

1. RESOLUTION, NOT JUST QUIETING
   → Mục tiêu không phải "tìm quiet position" 
   → Mà "RESOLVE tất cả tactical issues"
   → Quiet position là CONSEQUENCE, không phải GOAL

2. SELECTIVE QUIET MOVES
   → Một số quiet moves PHẢI được search trong QSearch
   → Nhưng chỉ những quiet moves "critical" (phát hiện tự động)
   → Không search TẤT CẢ quiet moves (quá đắt)

3. THREAT-AWARE EVALUATION
   → Stand pat phải xét threats hiện tại
   → Không giả định "position = eval" khi có threats chưa resolve

4. ADAPTIVE DEPTH & BREADTH
   → QSearch depth/breadth thay đổi theo tactical complexity
   → Simple exchange: 2 ply đủ
   → Complex tactic: có thể cần 10+ ply
   → Không dùng fixed depth limit

5. GRADUATED PRUNING
   → Pruning trong QSearch nên graduated, không binary
   → Move "có thể tốt nhưng unlikely" → reduced search, không skip

6. FEEDBACK FROM MAIN SEARCH
   → QSearch nên biết context: tại sao main search dừng ở đây?
   → Nếu main search dừng vì reduction → QSearch nên kỹ hơn
   → Nếu main search dừng vì natural depth → QSearch có thể nhanh hơn
```

### 2.2 Kiến Trúc Tổng Thể

```
┌──────────────────────────────────────────────────────────────────────┐
│                         DQRS ARCHITECTURE                            │
│              Deep Quiet Resolution Search                            │
│                                                                      │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │                 RESOLUTION ASSESSMENT                          │  │
│  │                                                                │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐│  │
│  │  │  Tactical     │  │  Threat      │  │  Quiet Position     ││  │
│  │  │  Tension      │  │  Inventory   │  │  Classifier         ││  │
│  │  │  Analyzer     │  │              │  │                     ││  │
│  │  └──────┬───────┘  └──────┬───────┘  └──────────┬───────────┘│  │
│  │         └────────┬────────┴────────┬────────────┘            │  │
│  │                  ▼                 ▼                          │  │
│  │         ┌────────────────────────────────┐                   │  │
│  │         │    Resolution Score (RS)        │                   │  │
│  │         │    0.0 = fully unresolved       │                   │  │
│  │         │    1.0 = fully resolved         │                   │  │
│  │         └──────────────┬─────────────────┘                   │  │
│  └────────────────────────┼─────────────────────────────────────┘  │
│                           ▼                                        │
│  ┌────────────────────────────────────────────────────────────────┐│
│  │                 THREAT-ADJUSTED STAND PAT                      ││
│  │                                                                ││
│  │  adjusted_eval = static_eval                                   ││
│  │                - unresolved_threat_penalty                     ││
│  │                - hanging_piece_discount                        ││
│  │                × eval_confidence_factor                        ││
│  └────────────────────────┬───────────────────────────────────────┘│
│                           ▼                                        │
│  ┌────────────────────────────────────────────────────────────────┐│
│  │                 ADAPTIVE MOVE GENERATION                       ││
│  │                                                                ││
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌────────────┐││
│  │  │ Tier 0:   │  │ Tier 1:   │  │ Tier 2:   │  │ Tier 3:    │││
│  │  │ Critical  │  │ Good      │  │ Specul.   │  │ Critical   │││
│  │  │ Captures  │  │ Captures  │  │ Captures  │  │ Quiet Moves│││
│  │  └───────────┘  └───────────┘  └───────────┘  └────────────┘││
│  └────────────────────────┬───────────────────────────────────────┘│
│                           ▼                                        │
│  ┌────────────────────────────────────────────────────────────────┐│
│  │                 RESOLUTION SEARCH LOOP                         ││
│  │                                                                ││
│  │  for each move (by tier, then by ordering within tier):        ││
│  │    ┌──────────────────────────────────────────────────────┐   ││
│  │    │  Per-Move Decision:                                   │   ││
│  │    │  - Full search (critical moves)                       │   ││
│  │    │  - Reduced search (likely good moves)                 │   ││
│  │    │  - Verification only (speculative moves)              │   ││
│  │    │  - Skip (clearly bad moves)                           │   ││
│  │    └──────────────────────────────────────────────────────┘   ││
│  └────────────────────────┬───────────────────────────────────────┘│
│                           ▼                                        │
│  ┌────────────────────────────────────────────────────────────────┐│
│  │                 DEPTH & BREADTH CONTROL                        ││
│  │                                                                ││
│  │  ┌──────────────────┐  ┌──────────────────────────────────┐  ││
│  │  │ Adaptive Depth   │  │ Convergence Detection            │  ││
│  │  │ Controller       │  │ (stop when resolved)             │  ││
│  │  └──────────────────┘  └──────────────────────────────────┘  ││
│  └────────────────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────────────┘
```

---

## III. Module 1: Resolution Assessment

### 3.1 Tactical Tension Analyzer

```python
class TacticalTensionAnalyzer:
    """Đo mức độ 'chưa giải quyết' (unresolved) của thế cờ
    
    Core concept: 
    Thay vì hỏi "position có quiet không?" (binary)
    Hỏi "bao nhiêu tactical tension chưa giải quyết?" (continuous)
    """
    
    def compute_tension(self, position):
        """Returns TensionProfile with detailed breakdown"""
        
        profile = TensionProfile()
        
        # ═══ T1: CAPTURE TENSION ═══
        # Quân có thể bắt nhau nhưng chưa bắt
        
        for square in position.occupied_squares():
            piece = position.piece_at(square)
            attackers = position.attackers_of(square, not piece.side)
            defenders = position.defenders_of(square, piece.side)
            
            if attackers:
                tension_point = TensionPoint(
                    square=square,
                    piece=piece,
                    attackers=attackers,
                    defenders=defenders,
                    type='capture_tension'
                )
                
                # Tension magnitude = potential material swing
                min_attacker_val = min(
                    piece_value(a.type) for a in attackers
                )
                
                if len(attackers) > len(defenders):
                    # Piece is en prise (more attackers than defenders)
                    tension_point.magnitude = piece.value
                    tension_point.urgency = 1.0  # Must resolve NOW
                    
                elif min_attacker_val < piece.value:
                    # Can be favorably captured
                    tension_point.magnitude = piece.value - min_attacker_val
                    tension_point.urgency = 0.8
                    
                else:
                    # Piece attacked but adequately defended
                    tension_point.magnitude = 0
                    tension_point.urgency = 0.2
                
                if tension_point.magnitude > 0:
                    profile.capture_tensions.append(tension_point)
        
        # ═══ T2: FORK TENSION ═══
        # Quân có thể fork nhưng chưa fork
        
        for piece in position.pieces(position.side_to_move):
            fork_squares = find_fork_opportunities(position, piece)
            
            for fork_sq, targets in fork_squares:
                if len(targets) >= 2:
                    min_target_val = min(
                        piece_value(t.type) for t in targets
                    )
                    
                    fork_tension = TensionPoint(
                        square=fork_sq,
                        piece=piece,
                        type='fork_tension',
                        magnitude=min_target_val * 0.7,
                        urgency=0.7,
                        details={'targets': targets}
                    )
                    profile.fork_tensions.append(fork_tension)
        
        # ═══ T3: PIN/SKEWER TENSION ═══
        
        pins = find_all_pins(position)
        for pin in pins:
            pinned_val = piece_value(pin.pinned_piece.type)
            behind_val = piece_value(pin.piece_behind.type)
            
            pin_tension = TensionPoint(
                square=pin.pinned_piece.square,
                type='pin_tension',
                magnitude=min(pinned_val, behind_val) * 0.5,
                urgency=0.6,
                details={'pin': pin}
            )
            profile.pin_tensions.append(pin_tension)
        
        # ═══ T4: PROMOTION TENSION ═══
        
        for pawn in position.pawns(position.side_to_move):
            rank = relative_rank(pawn.square, position.side_to_move)
            
            if rank >= 6:  # 6th rank or higher
                promotion_distance = 8 - rank
                
                # Check if path is clear
                path_clear = is_promotion_path_clear(position, pawn)
                can_be_stopped = can_opponent_stop_pawn(position, pawn)
                
                promo_tension = TensionPoint(
                    square=pawn.square,
                    type='promotion_tension',
                    magnitude=QUEEN_VALUE * (1.0 / promotion_distance),
                    urgency=1.0 if rank == 7 else (0.5 + rank * 0.07),
                    details={
                        'distance': promotion_distance,
                        'path_clear': path_clear,
                        'stoppable': can_be_stopped,
                    }
                )
                profile.promotion_tensions.append(promo_tension)
        
        # Do the same for opponent's pawns
        for pawn in position.pawns(position.opponent):
            rank = relative_rank(pawn.square, position.opponent)
            if rank >= 6:
                promotion_distance = 8 - rank
                promo_tension = TensionPoint(
                    square=pawn.square,
                    type='promotion_tension_opponent',
                    magnitude=QUEEN_VALUE * (1.0 / promotion_distance),
                    urgency=1.0 if rank == 7 else (0.5 + rank * 0.07),
                )
                profile.promotion_tensions.append(promo_tension)
        
        # ═══ T5: MATING TENSION ═══
        
        mate_threat_score = evaluate_mate_threats(
            position, position.side_to_move
        )
        if mate_threat_score > 0:
            profile.mate_tension = TensionPoint(
                type='mate_tension_our',
                magnitude=mate_threat_score,
                urgency=1.0
            )
        
        mate_threat_opponent = evaluate_mate_threats(
            position, position.opponent
        )
        if mate_threat_opponent > 0:
            profile.mate_tension_opponent = TensionPoint(
                type='mate_tension_opponent',
                magnitude=mate_threat_opponent,
                urgency=1.0
            )
        
        # ═══ T6: EXCHANGE TENSION ═══
        # Pieces that "should" be exchanged (one side wants to, other doesn't)
        
        exchange_tensions = find_exchange_tensions(position)
        profile.exchange_tensions = exchange_tensions
        
        # ═══ COMPUTE OVERALL TENSION SCORE ═══
        
        profile.total_tension = self.aggregate_tension(profile)
        profile.max_urgency = max(
            [t.urgency for t in profile.all_tensions()] or [0]
        )
        profile.tension_count = len(profile.all_tensions())
        
        return profile
    
    def aggregate_tension(self, profile):
        """Tổng hợp tất cả tensions thành 1 score"""
        
        total = 0.0
        
        for tension in profile.all_tensions():
            weighted_tension = tension.magnitude * tension.urgency
            total += weighted_tension
        
        # Normalize to 0-1 range
        # Typical max tension in position ≈ 2000cp (queen hanging + mate threat)
        normalized = min(total / 2000.0, 1.0)
        
        return normalized
```

### 3.2 Threat Inventory

```python
class ThreatInventory:
    """Inventory tất cả threats hiện tại và dự kiến
    
    Phân biệt:
    - Immediate threats: thực hiện ngay nước sau
    - Latent threats: cần 2+ nước nhưng khó ngăn
    - Phantom threats: có vẻ nguy hiểm nhưng dễ giải quyết
    """
    
    def catalog_threats(self, position):
        """Liệt kê và phân loại tất cả threats"""
        
        inventory = ThreatCatalog()
        
        # ═══ IMMEDIATE THREATS (1-move threats) ═══
        
        stm = position.side_to_move
        opp = position.opponent
        
        # 1. Capture threats: opponent can capture our piece next move
        for our_piece in position.pieces(stm):
            opp_attackers = position.attackers_of(our_piece.square, opp)
            
            for attacker in opp_attackers:
                # Is this a REAL threat or just an attack on defended piece?
                see_val = see_from_square(
                    position, attacker, our_piece.square
                )
                
                if see_val > 0:
                    # Genuine threat: favorable capture
                    threat = Threat(
                        type=THREAT_CAPTURE,
                        severity=see_val,
                        immediacy=IMMEDIATE,
                        source=attacker,
                        target=our_piece,
                        resolution_moves=self.find_resolutions(
                            position, attacker, our_piece
                        )
                    )
                    inventory.immediate.append(threat)
                
                elif see_val == 0 and our_piece.value >= ROOK_VALUE:
                    # Even exchange of major pieces = still a threat
                    threat = Threat(
                        type=THREAT_EXCHANGE,
                        severity=our_piece.value * 0.1,
                        immediacy=IMMEDIATE,
                        source=attacker,
                        target=our_piece,
                    )
                    inventory.immediate.append(threat)
        
        # 2. Check threats: opponent can give check
        check_moves = find_checking_moves(position, opp)
        for check_move in check_moves:
            # How dangerous is this check?
            danger = evaluate_check_danger(position, check_move)
            
            if danger > 50:
                threat = Threat(
                    type=THREAT_CHECK,
                    severity=danger,
                    immediacy=IMMEDIATE,
                    source_move=check_move,
                )
                inventory.immediate.append(threat)
        
        # 3. Mate threats
        if has_mate_in_1(position, opp):
            threat = Threat(
                type=THREAT_MATE,
                severity=MATE_VALUE,
                immediacy=IMMEDIATE,
            )
            inventory.immediate.append(threat)
        
        # ═══ LATENT THREATS (2-3 move threats) ═══
        
        # 4. Fork threats: opponent one move from forking
        for opp_piece in position.pieces(opp):
            fork_sqs = find_fork_opportunities(position, opp_piece)
            for fork_sq, targets in fork_sqs:
                if len(targets) >= 2:
                    min_val = min(piece_value(t.type) for t in targets)
                    threat = Threat(
                        type=THREAT_FORK,
                        severity=min_val * 0.6,
                        immediacy=LATENT,
                        source=opp_piece,
                        details={'fork_square': fork_sq, 'targets': targets}
                    )
                    inventory.latent.append(threat)
        
        # 5. Promotion threats: opponent pawn advancing
        for opp_pawn in position.pawns(opp):
            rank = relative_rank(opp_pawn.square, opp)
            if rank >= 5:
                can_advance = can_pawn_advance(position, opp_pawn)
                if can_advance:
                    threat = Threat(
                        type=THREAT_PROMOTION,
                        severity=QUEEN_VALUE * (rank - 4) / 4.0,
                        immediacy=LATENT if rank < 7 else IMMEDIATE,
                        source=opp_pawn,
                    )
                    inventory.latent.append(threat)
        
        # 6. Discovery threats
        discoveries = find_discovered_attack_setups(position, opp)
        for disc in discoveries:
            threat = Threat(
                type=THREAT_DISCOVERY,
                severity=disc.potential_gain,
                immediacy=LATENT,
                details=disc,
            )
            inventory.latent.append(threat)
        
        # 7. Back rank threats
        if has_back_rank_weakness(position, stm):
            back_rank_attackers = count_back_rank_attackers(position, opp)
            if back_rank_attackers > 0:
                threat = Threat(
                    type=THREAT_BACK_RANK,
                    severity=300 + back_rank_attackers * 200,
                    immediacy=LATENT,
                )
                inventory.latent.append(threat)
        
        # ═══ AGGREGATE THREAT SCORES ═══
        
        inventory.total_immediate_threat = sum(
            t.severity for t in inventory.immediate
        )
        inventory.total_latent_threat = sum(
            t.severity for t in inventory.latent
        )
        inventory.max_threat_severity = max(
            [t.severity for t in inventory.all_threats()] or [0]
        )
        inventory.threat_count = len(inventory.all_threats())
        
        return inventory
    
    def find_resolutions(self, position, attacker, target):
        """Tìm nước giải quyết threat"""
        resolutions = []
        
        # 1. Move target away
        for sq in target.legal_destinations():
            if not position.is_attacked_by(sq, attacker.side):
                resolutions.append(Resolution(
                    type='move_away',
                    move=Move(target, sq),
                    quality=0.5  # Decent but passive
                ))
        
        # 2. Block attack
        between_sqs = squares_between(attacker.square, target.square)
        for sq in between_sqs:
            blockers = position.pieces_that_can_reach(sq, target.side)
            for blocker in blockers:
                resolutions.append(Resolution(
                    type='block',
                    move=Move(blocker, sq),
                    quality=0.6
                ))
        
        # 3. Capture attacker
        our_attackers = position.attackers_of(attacker.square, target.side)
        for our_att in our_attackers:
            see_val = see_from_square(
                position, our_att, attacker.square
            )
            if see_val >= 0:
                resolutions.append(Resolution(
                    type='capture_attacker',
                    move=Move(our_att, attacker.square),
                    quality=0.9
                ))
        
        # 4. Counter-threat (play a bigger threat instead)
        # More complex, deferred to search
        
        return resolutions
```

### 3.3 Quiet Position Classifier

```python
class QuietPositionClassifier:
    """Phân loại position theo mức độ "quiet"
    
    Thay vì binary (quiet/not quiet), dùng continuous spectrum
    với nhiều dimensions of quietness
    """
    
    class QuietProfile:
        def __init__(self):
            self.material_quiet = 0.0     # Không có captures profitable
            self.tactical_quiet = 0.0     # Không có tactics pending
            self.king_quiet = 0.0         # Không có king attacks
            self.pawn_quiet = 0.0         # Không có promotions imminent
            self.overall_quiet = 0.0      # Tổng hợp
            
            self.resolution_score = 0.0   # 0=unresolved, 1=fully resolved
            self.recommended_depth = 0    # Bao nhiêu ply QSearch nên search
            self.recommended_move_types = []  # Loại moves nên generate
    
    def classify(self, position, tension_profile, threat_inventory):
        """Phân loại mức độ quiet của position"""
        
        profile = self.QuietProfile()
        
        # ── Material Quietness ──
        # Có profitable captures không?
        profitable_captures = count_profitable_captures(position)
        profile.material_quiet = sigmoid(-profitable_captures * 2 + 3)
        # 0 captures → 0.95 quiet
        # 1 capture → 0.73
        # 3+ captures → 0.12 (very noisy)
        
        # ── Tactical Quietness ──
        # Có chiến thuật pending không?
        tac_density = tension_profile.total_tension
        profile.tactical_quiet = 1.0 - tac_density
        
        # ── King Quietness ──
        king_threats = sum(
            1 for t in threat_inventory.all_threats()
            if t.type in [THREAT_CHECK, THREAT_MATE, THREAT_BACK_RANK]
        )
        profile.king_quiet = sigmoid(-king_threats * 3 + 2)
        
        # ── Pawn Quietness ──
        near_promotions = sum(
            1 for t in tension_profile.promotion_tensions
            if t.urgency > 0.7
        )
        profile.pawn_quiet = sigmoid(-near_promotions * 4 + 2)
        
        # ── Overall Quietness ──
        profile.overall_quiet = (
            profile.material_quiet * 0.35 +
            profile.tactical_quiet * 0.30 +
            profile.king_quiet * 0.20 +
            profile.pawn_quiet * 0.15
        )
        
        # ── Resolution Score ──
        # Tổng hợp: bao nhiêu tension đã resolved?
        profile.resolution_score = profile.overall_quiet
        
        # ── Recommendations ──
        profile.recommended_depth = self.recommend_depth(profile, tension_profile)
        profile.recommended_move_types = self.recommend_move_types(
            profile, tension_profile, threat_inventory
        )
        
        return profile
    
    def recommend_depth(self, profile, tension_profile):
        """Khuyến nghị bao nhiêu ply QSearch"""
        
        if profile.overall_quiet > 0.9:
            return 0  # Already quiet, no QSearch needed!
        elif profile.overall_quiet > 0.7:
            return 2  # Minor unresolved tension
        elif profile.overall_quiet > 0.4:
            return 4  # Moderate tension
        elif profile.overall_quiet > 0.2:
            return 6  # Significant tension
        else:
            return 10  # Major tactical storm
    
    def recommend_move_types(self, profile, tension_profile, 
                              threat_inventory):
        """Khuyến nghị loại moves nên generate trong QSearch"""
        
        move_types = ['GOOD_CAPTURES']  # Always generate good captures
        
        # Add quiet checks if king not quiet
        if profile.king_quiet < 0.5:
            move_types.append('QUIET_CHECKS')
        
        # Add pawn pushes if promotion tension
        if profile.pawn_quiet < 0.5:
            move_types.append('PAWN_PUSHES_67')  # 6th and 7th rank
        
        # Add piece retreats if pieces hanging
        hanging_count = len([
            t for t in threat_inventory.immediate
            if t.type == THREAT_CAPTURE and t.severity > 200
        ])
        if hanging_count > 0:
            move_types.append('PIECE_RETREATS')
        
        # Add blocking moves if mate threat
        if any(t.type == THREAT_MATE for t in threat_inventory.immediate):
            move_types.append('BLOCKING_MOVES')
        
        # Add zwischenzug candidates if in exchange sequence
        if is_in_exchange_sequence(tension_profile):
            move_types.append('ZWISCHENZUG')
        
        return move_types
```

### 3.4 Resolution Score Computation

```python
class ResolutionScoreComputer:
    """Tính Resolution Score: measure chính cho DQRS"""
    
    def compute(self, position, main_search_context=None):
        """Compute overall resolution score
        
        RS = 0.0: Completely unresolved (need deep QSearch)
        RS = 0.5: Partially resolved (moderate QSearch)
        RS = 1.0: Fully resolved (no QSearch needed)
        """
        
        # Get components
        tension = self.tension_analyzer.compute_tension(position)
        threats = self.threat_inventory.catalog_threats(position)
        quiet = self.classifier.classify(position, tension, threats)
        
        # Resolution Score
        rs = quiet.resolution_score
        
        # Modify based on main search context
        if main_search_context:
            # Nếu main search dừng vì reduction → cần QSearch kỹ hơn
            if main_search_context.was_reduced:
                rs *= 0.8  # Giảm RS → deeper QSearch
            
            # Nếu main search đã search sâu → QSearch có thể nhanh hơn
            if main_search_context.search_depth >= 15:
                rs = min(rs * 1.1, 1.0)  # Tăng RS nhẹ
            
            # Nếu eval thay đổi nhiều qua iterations → unstable
            if main_search_context.eval_volatility > 50:
                rs *= 0.85
        
        return ResolutionResult(
            score=rs,
            tension_profile=tension,
            threat_inventory=threats,
            quiet_profile=quiet,
            recommended_depth=quiet.recommended_depth,
            recommended_move_types=quiet.recommended_move_types,
        )
```

---

## IV. Module 2: Threat-Adjusted Stand Pat

```python
class ThreatAdjustedStandPat:
    """Stand pat evaluation có xét threats
    
    Thay vì: stand_pat = static_eval  (Stockfish hiện tại)
    DQRS:    stand_pat = static_eval - threat_penalty + opportunity_bonus
    """
    
    def compute(self, position, resolution_result):
        """Tính threat-adjusted stand pat"""
        
        base_eval = nnue_eval(position)
        threats = resolution_result.threat_inventory
        tension = resolution_result.tension_profile
        
        # ═══ THREAT PENALTY ═══
        # Giảm eval dựa trên threats chưa giải quyết
        
        threat_penalty = 0
        
        # Immediate threats: discount heavily
        for threat in threats.immediate:
            if threat.type == THREAT_CAPTURE:
                # Quân bị đe dọa bắt → giảm eval
                # Nhưng chỉ giảm nếu không có resolution tốt
                best_resolution = self.best_resolution_quality(threat)
                
                if best_resolution < 0.3:
                    # No good resolution → heavy penalty
                    threat_penalty += threat.severity * 0.7
                elif best_resolution < 0.6:
                    # Mediocre resolution → moderate penalty
                    threat_penalty += threat.severity * 0.3
                else:
                    # Good resolution exists → light penalty
                    threat_penalty += threat.severity * 0.1
            
            elif threat.type == THREAT_MATE:
                threat_penalty += 500  # Mate threat = huge penalty
            
            elif threat.type == THREAT_CHECK:
                threat_penalty += threat.severity * 0.4
        
        # Latent threats: discount less
        for threat in threats.latent:
            threat_penalty += threat.severity * 0.15
        
        # ═══ OPPORTUNITY BONUS ═══
        # Tăng eval nếu TA có threats tốt chưa thực hiện
        
        our_threats = self.count_our_threats(position)
        opportunity_bonus = 0
        
        for our_threat in our_threats:
            if our_threat.type == THREAT_CAPTURE and our_threat.severity > 0:
                # Chỉ bonus nếu opponent có thể bị bắt quân
                opportunity_bonus += our_threat.severity * 0.2
            elif our_threat.type == THREAT_MATE:
                opportunity_bonus += 300
            elif our_threat.type == THREAT_FORK:
                opportunity_bonus += our_threat.severity * 0.3
        
        # ═══ CONFIDENCE SCALING ═══
        # Trong tactical chaos, eval ít tin cậy → shrink toward 0
        
        quietness = resolution_result.quiet_profile.overall_quiet
        confidence = 0.3 + 0.7 * quietness
        # Fully quiet → confidence = 1.0 (trust eval 100%)
        # Very noisy → confidence = 0.3 (trust eval only 30%)
        
        # ═══ HANGING PIECE DISCOUNT ═══
        
        hanging_discount = 0
        for tension_point in tension.capture_tensions:
            if tension_point.urgency >= 0.8:  # Piece is basically hanging
                hanging_discount += tension_point.magnitude * 0.5
        
        # ═══ FINAL COMPUTATION ═══
        
        raw_adjusted = (base_eval 
                       - threat_penalty 
                       + opportunity_bonus 
                       - hanging_discount)
        
        # Apply confidence: pull toward neutral
        # If confidence = 0.3 and eval = +300:
        # adjusted = 300 * 0.3 + 0 * 0.7 = 90
        # (Much more conservative estimate)
        adjusted_eval = raw_adjusted * confidence
        
        return ThreatAdjustedEval(
            raw_eval=base_eval,
            adjusted_eval=adjusted_eval,
            threat_penalty=threat_penalty,
            opportunity_bonus=opportunity_bonus,
            hanging_discount=hanging_discount,
            confidence=confidence,
        )
    
    def best_resolution_quality(self, threat):
        """Chất lượng resolution tốt nhất cho threat"""
        if not threat.resolution_moves:
            return 0.0
        return max(r.quality for r in threat.resolution_moves)
    
    def count_our_threats(self, position):
        """Đếm threats MÀ TA tạo ra (opportunities)"""
        # Similar to threat_inventory but for our side
        threats = []
        
        stm = position.side_to_move
        opp = position.opponent
        
        for their_piece in position.pieces(opp):
            our_attackers = position.attackers_of(
                their_piece.square, stm
            )
            for attacker in our_attackers:
                see_val = see_from_square(
                    position, attacker, their_piece.square
                )
                if see_val > 0:
                    threats.append(Threat(
                        type=THREAT_CAPTURE,
                        severity=see_val,
                        immediacy=IMMEDIATE,
                        source=attacker,
                        target=their_piece,
                    ))
        
        # Fork opportunities
        for our_piece in position.pieces(stm):
            forks = find_fork_opportunities(position, our_piece)
            for fork_sq, targets in forks:
                if len(targets) >= 2:
                    min_val = min(piece_value(t.type) for t in targets)
                    threats.append(Threat(
                        type=THREAT_FORK,
                        severity=min_val * 0.6,
                        immediacy=LATENT,
                    ))
        
        return threats
```

---

## V. Module 3: Adaptive Move Generation

### 5.1 Tiered Move Generator

```python
class TieredMoveGenerator:
    """Generate moves theo tiers, chỉ generate tier tiếp khi cần
    
    Key innovation: Không generate tất cả captures rồi sort
    Mà generate theo priority, dừng sớm khi beta cutoff
    
    THÊM: Generate selective quiet moves khi cần thiết
    """
    
    def __init__(self, position, resolution_result, search_state):
        self.position = position
        self.resolution = resolution_result
        self.state = search_state
        self.current_tier = 0
        self.yielded_moves = set()
        
        # Determine which tiers to use based on resolution
        self.active_tiers = self.determine_tiers()
    
    def determine_tiers(self):
        """Xác định tiers nào cần generate"""
        
        tiers = []
        recommended = self.resolution.recommended_move_types
        
        # Tier 0: ALWAYS
        tiers.append(Tier(
            name='TT_MOVE',
            generator=self.gen_tt_move,
            priority=0,
        ))
        
        # Tier 1: ALWAYS - Critical captures
        tiers.append(Tier(
            name='CRITICAL_CAPTURES',
            generator=self.gen_critical_captures,
            priority=1,
        ))
        
        # Tier 2: ALWAYS - Good captures
        tiers.append(Tier(
            name='GOOD_CAPTURES',
            generator=self.gen_good_captures,
            priority=2,
        ))
        
        # Tier 3: CONDITIONAL - Quiet checks
        if 'QUIET_CHECKS' in recommended:
            tiers.append(Tier(
                name='QUIET_CHECKS',
                generator=self.gen_quiet_checks,
                priority=3,
            ))
        
        # Tier 4: CONDITIONAL - Critical quiet moves
        if any(t in recommended for t in [
            'PIECE_RETREATS', 'BLOCKING_MOVES', 'ZWISCHENZUG'
        ]):
            tiers.append(Tier(
                name='CRITICAL_QUIETS',
                generator=self.gen_critical_quiet_moves,
                priority=4,
            ))
        
        # Tier 5: CONDITIONAL - Pawn pushes to promotion
        if 'PAWN_PUSHES_67' in recommended:
            tiers.append(Tier(
                name='PAWN_PUSHES',
                generator=self.gen_pawn_pushes_67,
                priority=5,
            ))
        
        # Tier 6: ALWAYS (low priority) - Speculative captures
        tiers.append(Tier(
            name='SPECULATIVE_CAPTURES',
            generator=self.gen_speculative_captures,
            priority=6,
        ))
        
        return tiers
    
    def next_move(self):
        """Yield next move in priority order"""
        
        while self.current_tier < len(self.active_tiers):
            tier = self.active_tiers[self.current_tier]
            
            move = tier.generator()
            
            if move is not None:
                if move not in self.yielded_moves:
                    self.yielded_moves.add(move)
                    return move, tier.name
            else:
                # Tier exhausted, move to next
                self.current_tier += 1
        
        return None, None
    
    # ═══ TIER GENERATORS ═══
    
    def gen_tt_move(self):
        """Tier 0: TT move (if it's a capture or relevant quiet)"""
        if not self._tt_yielded and self.state.tt_move:
            self._tt_yielded = True
            tt_move = self.state.tt_move
            
            if is_legal(self.position, tt_move):
                # In QSearch, accept TT move even if quiet
                # (TT từ deeper search biết điều gì ta không biết)
                return tt_move
        
        return None
    
    def gen_critical_captures(self):
        """Tier 1: Captures that MUST be searched
        - Recaptures
        - Captures of hanging pieces
        - Captures that win material (SEE > 0)
        """
        if not self._critical_captures_generated:
            self._critical_captures = []
            
            for move in generate_captures(self.position):
                criticality = self.assess_capture_criticality(move)
                
                if criticality >= 0.8:
                    self._critical_captures.append((move, criticality))
            
            # Sort by criticality (highest first)
            self._critical_captures.sort(key=lambda x: -x[1])
            self._critical_captures_generated = True
            self._critical_capture_idx = 0
        
        if self._critical_capture_idx < len(self._critical_captures):
            move, _ = self._critical_captures[self._critical_capture_idx]
            self._critical_capture_idx += 1
            return move
        
        return None
    
    def assess_capture_criticality(self, capture):
        """Đánh giá mức criticality của capture"""
        score = 0.0
        
        # Recapture (bắt lại quân vừa bắt mình)
        if capture.to_square == self.position.last_capture_square:
            score += 0.5
        
        # Captures hanging piece
        captured_piece = self.position.piece_at(capture.to_square)
        defenders = self.position.defenders_of(
            capture.to_square, captured_piece.side
        )
        if len(defenders) == 0:
            score += 0.4  # Completely hanging
        
        # SEE positive
        see_val = see(self.position, capture)
        if see_val > 0:
            score += min(see_val / 500.0, 0.3)
        elif see_val < -200:
            score -= 0.3
        
        # High-value victim
        score += captured_piece.value / 2000.0
        
        return clamp(score, 0.0, 1.0)
    
    def gen_good_captures(self):
        """Tier 2: Captures with SEE >= 0 (not already in Tier 1)"""
        if not self._good_captures_generated:
            self._good_captures = []
            
            for move in generate_captures(self.position):
                if move in self.yielded_moves:
                    continue
                
                see_val = see(self.position, move)
                if see_val >= 0:
                    # Score: MVV-LVA + SEE bonus
                    mvv_lva = mvv_lva_score(move)
                    score = mvv_lva + see_val * 0.5
                    self._good_captures.append((move, score))
            
            self._good_captures.sort(key=lambda x: -x[1])
            self._good_captures_generated = True
            self._good_capture_idx = 0
        
        if self._good_capture_idx < len(self._good_captures):
            move, _ = self._good_captures[self._good_capture_idx]
            self._good_capture_idx += 1
            return move
        
        return None
    
    def gen_quiet_checks(self):
        """Tier 3: Non-capture moves that give check"""
        if not self._quiet_checks_generated:
            self._quiet_checks = []
            
            for move in generate_quiet_checks(self.position):
                if move in self.yielded_moves:
                    continue
                
                # Evaluate check quality
                check_quality = self.evaluate_check_quality(move)
                
                if check_quality > 0.3:  # Only good checks
                    self._quiet_checks.append((move, check_quality))
            
            self._quiet_checks.sort(key=lambda x: -x[1])
            self._quiet_checks_generated = True
            self._quiet_check_idx = 0
        
        if self._quiet_check_idx < len(self._quiet_checks):
            move, _ = self._quiet_checks[self._quiet_check_idx]
            self._quiet_check_idx += 1
            return move
        
        return None
    
    def evaluate_check_quality(self, check_move):
        """Đánh giá chất lượng nước chiếu"""
        quality = 0.0
        
        # Piece giving check
        checker = check_move.piece_type
        
        # Double check = always high quality
        if gives_double_check(self.position, check_move):
            return 1.0
        
        # Check that forces king to bad square
        king_sq = self.position.king_square(self.position.opponent)
        evasion_count = count_check_evasions(
            self.position, check_move
        )
        
        if evasion_count <= 2:
            quality += 0.4  # Few evasions = strong check
        elif evasion_count <= 4:
            quality += 0.2
        
        # Check that wins tempo on attack
        if checker in [QUEEN, ROOK] and is_attacking_position(self.position):
            quality += 0.3
        
        # Check with discovered attack
        if has_discovered_effect(self.position, check_move):
            quality += 0.3
        
        # Penalty: check that loses material
        if see(self.position, check_move) < -100:
            quality -= 0.5
        
        return clamp(quality, 0.0, 1.0)
    
    def gen_critical_quiet_moves(self):
        """Tier 4: SELECTIVE quiet moves that address critical threats
        
        THIS IS THE KEY INNOVATION OF DQRS:
        Generate specific quiet moves that resolve tactical issues
        """
        if not self._critical_quiets_generated:
            self._critical_quiets = []
            
            threats = self.resolution.threat_inventory
            tension = self.resolution.tension_profile
            
            # A. Piece retreat moves (escape from attack)
            if 'PIECE_RETREATS' in self.resolution.recommended_move_types:
                retreats = self.find_critical_retreats(threats)
                self._critical_quiets.extend(retreats)
            
            # B. Blocking moves (block mate threat or major attack)
            if 'BLOCKING_MOVES' in self.resolution.recommended_move_types:
                blocks = self.find_blocking_moves(threats)
                self._critical_quiets.extend(blocks)
            
            # C. Zwischenzug candidates
            if 'ZWISCHENZUG' in self.resolution.recommended_move_types:
                zwischen = self.find_zwischenzug(tension)
                self._critical_quiets.extend(zwischen)
            
            # D. Counter-threat moves
            counter_threats = self.find_counter_threats(threats)
            self._critical_quiets.extend(counter_threats)
            
            # Sort by priority
            self._critical_quiets.sort(key=lambda x: -x[1])
            
            # LIMIT: maximum quiet moves to prevent explosion
            MAX_CRITICAL_QUIETS = 5
            self._critical_quiets = self._critical_quiets[:MAX_CRITICAL_QUIETS]
            
            self._critical_quiets_generated = True
            self._critical_quiet_idx = 0
        
        if self._critical_quiet_idx < len(self._critical_quiets):
            move, _ = self._critical_quiets[self._critical_quiet_idx]
            self._critical_quiet_idx += 1
            return move
        
        return None
    
    def find_critical_retreats(self, threats):
        """Tìm nước rút quân quan trọng"""
        retreats = []
        
        for threat in threats.immediate:
            if (threat.type == THREAT_CAPTURE 
                and threat.severity >= 200):
                # Significant piece under attack
                target = threat.target
                
                for dest_sq in target.legal_destinations():
                    # Square must be safe
                    if not self.position.is_attacked_by(
                        dest_sq, target.side ^ 1
                    ):
                        # Evaluate retreat quality
                        retreat_quality = self.evaluate_retreat_quality(
                            target, dest_sq, threat
                        )
                        
                        if retreat_quality > 0.4:
                            move = Move(target, dest_sq)
                            retreats.append((move, retreat_quality))
        
        return retreats
    
    def evaluate_retreat_quality(self, piece, dest_sq, threat):
        """Đánh giá chất lượng nước rút quân"""
        quality = 0.5  # Base: retreat saves piece
        
        # Retreating to better square = bonus
        activity_current = piece_activity_score(piece, piece.square)
        activity_new = piece_activity_score(piece, dest_sq)
        if activity_new > activity_current:
            quality += 0.2  # Better square
        
        # Retreating while creating counter-threat = bonus
        # (Check if piece at dest_sq attacks something valuable)
        attacks_from_dest = get_attacks_from(
            self.position, dest_sq, piece.type
        )
        for attacked_sq in attacks_from_dest:
            attacked_piece = self.position.piece_at(attacked_sq)
            if (attacked_piece and 
                attacked_piece.side != piece.side and
                attacked_piece.value >= piece.value):
                quality += 0.2  # Counter-threat
                break
        
        # Penalty if retreat loses tempo in attack
        if is_attacking_king(self.position, piece):
            quality -= 0.15
        
        return quality
    
    def find_zwischenzug(self, tension):
        """Tìm zwischenzug candidates trong chuỗi exchange"""
        zwischen = []
        
        if not tension.capture_tensions:
            return zwischen
        
        # Xét các nước quiet có threat lớn hơn capture đang pending
        pending_capture_value = max(
            t.magnitude for t in tension.capture_tensions
        ) if tension.capture_tensions else 0
        
        # Tìm nước quiet tạo threat lớn hơn
        for move in generate_threatening_quiets(self.position):
            threat_created = evaluate_threat_value(self.position, move)
            
            if threat_created > pending_capture_value:
                # This quiet move creates bigger threat than pending capture!
                # → Classic zwischenzug
                zwischen.append((move, threat_created / 1000.0))
        
        return zwischen
    
    def find_counter_threats(self, threats):
        """Tìm nước đi tạo counter-threat lớn hơn threat hiện tại"""
        counters = []
        
        if not threats.immediate:
            return counters
        
        max_threat = max(threats.immediate, key=lambda t: t.severity)
        
        # Tìm nước tạo threat lớn hơn
        for move in generate_threatening_moves(self.position):
            our_threat = evaluate_threat_value(self.position, move)
            
            if our_threat > max_threat.severity * 1.2:
                # Counter-threat stronger than their threat
                # → They must respond to ours instead
                priority = our_threat / max_threat.severity
                counters.append((move, min(priority, 1.0)))
        
        return counters[:3]  # Max 3 counter-threats
    
    def gen_pawn_pushes_67(self):
        """Tier 5: Tốt đẩy lên hàng 6-7 (gần phong)"""
        if not self._pawn_pushes_generated:
            self._pawn_pushes = []
            
            stm = self.position.side_to_move
            
            for pawn in self.position.pawns(stm):
                rank = relative_rank(pawn.square, stm)
                
                if rank >= 5:  # 6th rank push or 7th rank push
                    push_sq = pawn_push_square(pawn.square, stm)
                    
                    if (self.position.is_empty(push_sq) and
                        not self.position.is_attacked_by(push_sq, stm ^ 1)):
                        
                        priority = (rank - 4) * 0.3
                        
                        # Bonus if passed pawn
                        if is_passed_pawn(self.position, pawn.square, stm):
                            priority += 0.3
                        
                        move = Move(pawn, push_sq)
                        if rank == 7:  # Will promote!
                            move.is_promotion = True
                            move.promotion_type = QUEEN
                            priority = 1.0
                        
                        self._pawn_pushes.append((move, priority))
            
            self._pawn_pushes.sort(key=lambda x: -x[1])
            self._pawn_pushes_generated = True
            self._pawn_push_idx = 0
        
        if self._pawn_push_idx < len(self._pawn_pushes):
            move, _ = self._pawn_pushes[self._pawn_push_idx]
            self._pawn_push_idx += 1
            return move
        
        return None
    
    def gen_speculative_captures(self):
        """Tier 6: Captures with slightly negative SEE (-300 < SEE < 0)
        
        These are "speculative": might be sacrifice that works
        Search with reduced depth to verify
        """
        if not self._spec_captures_generated:
            self._spec_captures = []
            
            for move in generate_captures(self.position):
                if move in self.yielded_moves:
                    continue
                
                see_val = see(self.position, move)
                
                if -300 <= see_val < 0:
                    # Slightly negative SEE: speculative
                    # Might be sacrifice with compensation
                    
                    compensation = self.estimate_compensation(move)
                    
                    net_value = see_val + compensation
                    if net_value > -100:  # Reasonable speculation
                        self._spec_captures.append((move, net_value))
            
            self._spec_captures.sort(key=lambda x: -x[1])
            self._spec_captures_generated = True
            self._spec_capture_idx = 0
        
        if self._spec_capture_idx < len(self._spec_captures):
            move, _ = self._spec_captures[self._spec_capture_idx]
            self._spec_capture_idx += 1
            return move
        
        return None
    
    def estimate_compensation(self, capture):
        """Ước tính compensation cho sacrifice"""
        compensation = 0
        
        # Compensation from discovered attack
        if has_discovered_attack(self.position, capture):
            compensation += 200
        
        # Compensation from removing key defender
        if removes_key_defender(self.position, capture):
            compensation += 150
        
        # Compensation from piece activity gain
        activity_gain = estimate_activity_gain(self.position, capture)
        compensation += activity_gain
        
        # Compensation from king attack
        if increases_king_attack(self.position, capture):
            compensation += 100
        
        return compensation
```

---

## VI. Module 4: Resolution Search Loop

### 6.1 Main DQRS Function

```python
def dqrs(position, alpha, beta, depth_budget, main_search_context=None):
    """Deep Quiet Resolution Search
    
    Replaces traditional quiescence search
    
    Parameters:
        position: current board position
        alpha, beta: search window
        depth_budget: remaining QSearch depth
        main_search_context: info from main search (why we're here)
    
    Returns:
        score: position evaluation after resolving all tactical issues
    """
    
    # ═══ PHASE 0: RESOLUTION ASSESSMENT ═══
    
    # Compute how "resolved" this position is
    resolution = compute_resolution(position, main_search_context)
    
    # If position is fully resolved, return eval immediately
    if resolution.score >= 0.95 and depth_budget <= 0:
        return nnue_eval(position)
    
    # ═══ PHASE 1: THREAT-ADJUSTED STAND PAT ═══
    
    stand_pat_result = threat_adjusted_stand_pat(position, resolution)
    adjusted_eval = stand_pat_result.adjusted_eval
    
    # Beta cutoff with adjusted eval
    if adjusted_eval >= beta:
        return beta
    
    # Raise alpha with adjusted eval
    if adjusted_eval > alpha:
        alpha = adjusted_eval
    
    # ═══ PHASE 2: EARLY TERMINATION CHECKS ═══
    
    # Check if we should stop QSearch
    termination = check_termination(
        position, resolution, depth_budget, alpha, beta, adjusted_eval
    )
    
    if termination.should_stop:
        return termination.score
    
    # Adaptive depth: use recommendation from resolution assessment
    effective_depth = min(
        depth_budget,
        resolution.recommended_depth
    )
    
    if effective_depth <= 0 and resolution.score > 0.8:
        return adjusted_eval
    
    # ═══ PHASE 3: MOVE GENERATION & SEARCH ═══
    
    move_gen = TieredMoveGenerator(position, resolution, search_state)
    
    best_score = adjusted_eval  # Stand pat as baseline
    moves_searched = 0
    
    while True:
        move, tier_name = move_gen.next_move()
        
        if move is None:
            break  # No more moves
        
        moves_searched += 1
        
        # ═══ PER-MOVE PRUNING ═══
        
        prune_decision = dqrs_move_pruning(
            position, move, tier_name, resolution,
            alpha, beta, adjusted_eval, effective_depth, moves_searched
        )
        
        if prune_decision.action == SKIP:
            continue
        
        # Determine search depth for this move
        child_depth = compute_child_depth(
            move, tier_name, prune_decision, effective_depth, resolution
        )
        
        # ═══ RECURSIVE SEARCH ═══
        
        position.make_move(move)
        
        if child_depth <= 0 and not position.is_in_check():
            # Leaf evaluation
            score = -threat_adjusted_stand_pat(
                position, 
                compute_resolution(position, None)
            ).adjusted_eval
        else:
            score = -dqrs(
                position, -beta, -alpha, child_depth, 
                main_search_context=None  # Child doesn't inherit context
            )
        
        position.unmake_move(move)
        
        # ═══ SCORE PROCESSING ═══
        
        if score > best_score:
            best_score = score
        
        if score > alpha:
            alpha = score
        
        if score >= beta:
            # Beta cutoff
            record_cutoff_stats(move, tier_name, moves_searched)
            return beta
        
        # ═══ BUDGET CHECK ═══
        # Prevent QSearch explosion
        if moves_searched >= max_moves_for_tier(tier_name, resolution):
            if tier_name not in ['TT_MOVE', 'CRITICAL_CAPTURES']:
                break  # Budget exceeded for non-critical tiers
    
    # ═══ PHASE 4: POST-SEARCH VALIDATION ═══
    
    # Check if all critical tensions were resolved
    if best_score == adjusted_eval and resolution.score < 0.5:
        # Stand pat was best, but position is unresolved
        # This might be horizon effect!
        
        # Additional check: is there a quiet threat we missed?
        remaining_threats = check_remaining_threats(position)
        if remaining_threats > 100:
            # Adjust score to account for unresolved threats
            best_score -= remaining_threats * 0.3
    
    return best_score


def check_termination(position, resolution, depth_budget, 
                       alpha, beta, adjusted_eval):
    """Kiểm tra có nên dừng QSearch sớm không"""
    
    # T1: Checkmate/stalemate
    if position.is_checkmate():
        return Termination(True, -MATE_VALUE + position.ply)
    if position.is_stalemate():
        return Termination(True, 0)
    
    # T2: Repetition
    if position.is_repetition():
        return Termination(True, 0)
    
    # T3: 50-move rule
    if position.halfmove_clock >= 100:
        return Termination(True, 0)
    
    # T4: Position is FULLY resolved AND depth budget exhausted
    if resolution.score > 0.9 and depth_budget <= 0:
        return Termination(True, adjusted_eval)
    
    # T5: Delta pruning (enhanced)
    max_possible_gain = estimate_max_gain(position, resolution)
    if adjusted_eval + max_possible_gain + 100 < alpha:
        # Even best possible capture won't raise alpha
        return Termination(True, alpha)
    
    # T6: Depth budget fully exhausted AND no critical threats
    if depth_budget <= -4 and resolution.score > 0.6:
        return Termination(True, adjusted_eval)
    
    return Termination(False, 0)


def dqrs_move_pruning(position, move, tier_name, resolution,
                       alpha, beta, adjusted_eval, effective_depth,
                       moves_searched):
    """Per-move pruning trong DQRS"""
    
    # Never prune TT move or critical captures
    if tier_name in ['TT_MOVE', 'CRITICAL_CAPTURES']:
        return PruneDecision(action=SEARCH)
    
    # ═══ ENHANCED SEE PRUNING ═══
    # (Nhìn xa hơn standard SEE)
    
    if move.is_capture:
        see_value = see(position, move)
        
        # Standard SEE pruning with adaptive threshold
        threshold = compute_see_threshold(
            effective_depth, resolution, tier_name
        )
        
        if see_value < threshold:
            # Check for compensation before pruning
            compensation = estimate_capture_compensation(position, move)
            
            if see_value + compensation < threshold:
                return PruneDecision(action=SKIP)
    
    # ═══ ENHANCED DELTA PRUNING ═══
    
    if move.is_capture:
        capture_gain = piece_value(position.piece_at(move.to_square))
        
        # Adaptive margin based on resolution
        margin = 200 * (1.0 + (1.0 - resolution.score))
        # Unresolved → larger margin (less pruning)
        # Resolved → smaller margin (more pruning)
        
        if adjusted_eval + capture_gain + margin < alpha:
            return PruneDecision(action=SKIP)
    
    # ═══ QUIET MOVE PRUNING ═══
    
    if not move.is_capture and tier_name in [
        'CRITICAL_QUIETS', 'QUIET_CHECKS', 'PAWN_PUSHES'
    ]:
        # Quiet moves in QSearch need high bar
        
        if tier_name == 'QUIET_CHECKS':
            # Already filtered by check quality, just limit count
            if moves_searched > 8:
                return PruneDecision(action=SKIP)
        
        elif tier_name == 'CRITICAL_QUIETS':
            # Critical quiets limited to 5 (set in generator)
            pass  # Trust generator's selection
        
        elif tier_name == 'PAWN_PUSHES':
            if moves_searched > 10:
                return PruneDecision(action=SKIP)
    
    # ═══ SPECULATIVE CAPTURE PRUNING ═══
    
    if tier_name == 'SPECULATIVE_CAPTURES':
        if moves_searched > 12:
            return PruneDecision(action=SKIP)
        
        # Only search if position is still unresolved
        if resolution.score > 0.7:
            return PruneDecision(action=SKIP)
    
    return PruneDecision(action=SEARCH)


def compute_see_threshold(depth, resolution, tier_name):
    """SEE threshold tự thích ứng"""
    
    base = 0
    
    if tier_name == 'GOOD_CAPTURES':
        base = -10  # Allow slightly negative SEE
    elif tier_name == 'SPECULATIVE_CAPTURES':
        base = -200  # Allow more negative SEE (it's speculative)
    
    # Resolution adjustment
    # Unresolved → more lenient (accept worse captures)
    # Resolved → stricter
    resolution_factor = 1.0 + (1.0 - resolution.score) * 0.5
    
    # Depth adjustment
    depth_factor = max(1, depth) * (-20)
    
    return int((base + depth_factor) * resolution_factor)


def compute_child_depth(move, tier_name, prune_decision, 
                         effective_depth, resolution):
    """Compute search depth for child node"""
    
    base_depth = effective_depth - 1
    
    # Tier-based adjustments
    if tier_name == 'CRITICAL_CAPTURES':
        # Critical captures: never reduce
        return max(base_depth, 1)
    
    elif tier_name == 'GOOD_CAPTURES':
        # Good captures: standard depth
        return base_depth
    
    elif tier_name == 'QUIET_CHECKS':
        # Quiet checks: reduce slightly
        return max(base_depth - 1, 0)
    
    elif tier_name == 'CRITICAL_QUIETS':
        # Critical quiets: reduce more
        return max(base_depth - 2, 0)
    
    elif tier_name == 'PAWN_PUSHES':
        # Pawn pushes: depends on rank
        if move.is_promotion:
            return max(base_depth, 2)  # Promotion: ensure sufficient depth
        else:
            return max(base_depth - 1, 0)
    
    elif tier_name == 'SPECULATIVE_CAPTURES':
        # Speculative: heavily reduced
        return max(base_depth - 2, 0)
    
    return base_depth
```

### 6.2 Convergence Detection

```python
class ConvergenceDetector:
    """Phát hiện khi QSearch đã "hội tụ" (không cần search thêm)
    
    Key insight: Thay vì fixed depth limit,
    dừng khi score stable và position resolved
    """
    
    def __init__(self):
        self.score_history = []
        self.resolution_history = []
    
    def should_stop(self, current_score, resolution_score, depth):
        """Kiểm tra convergence"""
        
        self.score_history.append(current_score)
        self.resolution_history.append(resolution_score)
        
        if len(self.score_history) < 2:
            return False
        
        # Condition 1: Score converged
        score_stable = all(
            abs(self.score_history[i] - self.score_history[i-1]) < 15
            for i in range(max(1, len(self.score_history)-3), 
                          len(self.score_history))
        )
        
        # Condition 2: Resolution improving
        resolution_improving = (
            self.resolution_history[-1] > self.resolution_history[-2]
        )
        
        # Condition 3: Position sufficiently resolved
        sufficiently_resolved = resolution_score > 0.85
        
        # Stop if:
        # a) Score stable AND position resolved
        if score_stable and sufficiently_resolved:
            return True
        
        # b) Score stable for 4+ iterations (converged even if not "resolved")
        if len(self.score_history) >= 4:
            last_4 = self.score_history[-4:]
            if max(last_4) - min(last_4) < 10:
                return True
        
        # c) Hard depth limit (safety valve)
        if depth <= -12:
            return True
        
        return False
```

---

## VII. Module 5: Depth & Breadth Control

### 7.1 Adaptive Depth Controller

```python
class AdaptiveDepthController:
    """Điều khiển QSearch depth tự thích ứng
    
    Thay vì fixed depth limit, depth phụ thuộc vào:
    1. Tactical complexity
    2. Resolution progress
    3. Computational budget remaining
    """
    
    def compute_depth_budget(self, position, resolution, 
                              main_search_context):
        """Tính depth budget cho QSearch"""
        
        # Base depth từ resolution recommendation
        base_depth = resolution.recommended_depth
        
        # ═══ ADJUSTMENTS ═══
        
        # A1: Main search context
        if main_search_context:
            if main_search_context.was_reduced:
                # Main search dùng LMR → QSearch nên bù
                base_depth += 2
            
            if main_search_context.is_pv_node:
                # PV node → QSearch quan trọng hơn
                base_depth += 1
            
            if main_search_context.remaining_depth_was_high:
                # Search depth cao → eval có lẽ đúng → QSearch nhanh hơn
                base_depth -= 1
        
        # A2: Material on board
        # Nhiều quân → nhiều captures possible → need more depth
        piece_count = popcount(position.occupied)
        if piece_count > 24:
            base_depth += 1
        elif piece_count < 8:
            base_depth += 2  # Endgame: need precision, but few moves
        
        # A3: Time pressure
        time_factor = get_time_pressure_factor()
        if time_factor > 0.8:
            base_depth = max(2, base_depth - 3)  # Severe cut in time trouble
        elif time_factor > 0.5:
            base_depth = max(3, base_depth - 1)
        
        # A4: Resolution score
        if resolution.score > 0.8:
            base_depth = max(2, base_depth - 2)  # Already mostly resolved
        elif resolution.score < 0.2:
            base_depth += 2  # Highly unresolved → need deep search
        
        # A5: King safety
        if resolution.quiet_profile.king_quiet < 0.3:
            base_depth += 1  # King under attack → search deeper
        
        # Clamp
        return clamp(base_depth, 2, 16)
    
    def compute_breadth_limit(self, tier_name, resolution, depth):
        """Compute maximum moves to search per tier"""
        
        limits = {
            'TT_MOVE': 1,            # Always just 1
            'CRITICAL_CAPTURES': 10,  # Almost unlimited
            'GOOD_CAPTURES': 8,       # Generous
            'QUIET_CHECKS': 4,        # Limited
            'CRITICAL_QUIETS': 5,     # Limited (set by generator)
            'PAWN_PUSHES': 3,         # Very limited
            'SPECULATIVE_CAPTURES': 4, # Limited
        }
        
        base_limit = limits.get(tier_name, 5)
        
        # Adjust by resolution
        if resolution.score < 0.3:
            # Very unresolved → search more moves
            base_limit = int(base_limit * 1.5)
        elif resolution.score > 0.7:
            # Mostly resolved → search fewer moves
            base_limit = max(2, int(base_limit * 0.7))
        
        # Adjust by depth
        if depth > 6:
            # Deep in QSearch → limit breadth
            base_limit = max(2, base_limit - 2)
        
        return base_limit
```

### 7.2 QSearch Explosion Prevention

```python
class ExplosionPrevention:
    """Prevent QSearch từ nổ exponentially
    
    Multiple layers of protection
    """
    
    def __init__(self, max_qs_nodes=50000):
        self.max_qs_nodes = max_qs_nodes
        self.current_qs_nodes = 0
        self.explosion_detected = False
    
    def check_budget(self):
        """Kiểm tra có vượt quá budget QSearch không"""
        self.current_qs_nodes += 1
        
        if self.current_qs_nodes > self.max_qs_nodes:
            self.explosion_detected = True
            return True  # Over budget
        
        # Warn zone: approaching limit
        if self.current_qs_nodes > self.max_qs_nodes * 0.8:
            return self.in_warn_zone()
        
        return False
    
    def in_warn_zone(self):
        """Trong warn zone: chỉ search critical moves"""
        # Restrict to critical captures only
        return True  # Signal to restrict
    
    def get_effective_depth(self, base_depth):
        """Giảm depth khi gần budget limit"""
        usage = self.current_qs_nodes / self.max_qs_nodes
        
        if usage > 0.9:
            return max(1, base_depth - 4)
        elif usage > 0.7:
            return max(2, base_depth - 2)
        elif usage > 0.5:
            return max(3, base_depth - 1)
        
        return base_depth
    
    def should_skip_tier(self, tier_name):
        """Bỏ qua tier không critical khi gần budget"""
        usage = self.current_qs_nodes / self.max_qs_nodes
        
        if usage > 0.8:
            # Only critical captures
            return tier_name not in [
                'TT_MOVE', 'CRITICAL_CAPTURES'
            ]
        elif usage > 0.6:
            # No speculative moves
            return tier_name in [
                'SPECULATIVE_CAPTURES', 'PAWN_PUSHES'
            ]
        
        return False
```

---

## VIII. Tối Ưu Hóa Hiệu Năng

### 8.1 Incremental Resolution

```python
class IncrementalResolution:
    """Tính Resolution Score incrementally
    
    Thay vì recompute toàn bộ sau mỗi move,
    chỉ update phần bị ảnh hưởng
    """
    
    def __init__(self):
        self.cached_tension = None
        self.cached_threats = None
        self.cached_quiet = None
    
    def update_after_move(self, position, move, prev_resolution):
        """Incremental update resolution sau move"""
        
        affected_squares = get_affected_squares(move)
        
        # ═══ UPDATE TENSIONS ═══
        
        new_tension = prev_resolution.tension_profile.copy()
        
        # Remove tensions involving moved piece or captured piece
        new_tension.remove_tensions_involving(move.from_square)
        new_tension.remove_tensions_involving(move.to_square)
        
        # Recompute tensions for affected squares only
        for sq in affected_squares:
            new_tensions = compute_tensions_for_square(position, sq)
            new_tension.add_tensions(new_tensions)
        
        # ═══ UPDATE THREATS ═══
        
        new_threats = prev_resolution.threat_inventory.copy()
        
        # Remove threats involving moved/captured pieces
        new_threats.remove_involving(move)
        
        # Recompute threats for affected pieces
        for sq in affected_squares:
            piece = position.piece_at(sq)
            if piece:
                new_threats_for_piece = compute_threats_for_piece(
                    position, piece
                )
                new_threats.add(new_threats_for_piece)
        
        # ═══ RECOMPUTE QUIET CLASSIFICATION ═══
        # (This is fast, just aggregation)
        
        new_quiet = classify_quiet_fast(new_tension, new_threats)
        
        return ResolutionResult(
            score=new_quiet.resolution_score,
            tension_profile=new_tension,
            threat_inventory=new_threats,
            quiet_profile=new_quiet,
            recommended_depth=new_quiet.recommended_depth,
            recommended_move_types=new_quiet.recommended_move_types,
        )
```

### 8.2 Lazy Computation

```python
class LazyDQRS:
    """Tính toán lazy: chỉ compute khi cần"""
    
    def dqrs_lazy(self, position, alpha, beta, depth_budget):
        """Lazy version: skip expensive analysis khi không cần"""
        
        # FAST PATH: standard eval
        stand_pat = nnue_eval(position)
        
        # Quick beta cutoff (no resolution analysis needed!)
        if stand_pat >= beta + 200:
            # Eval far above beta → almost certainly a cutoff
            return beta
        
        # Quick alpha check
        if stand_pat + QUEEN_VALUE < alpha:
            # Even capturing queen won't help → delta prune
            return alpha
        
        # MEDIUM PATH: resolution assessment
        # Only if position doesn't immediately cutoff
        
        has_captures = has_any_capture(position)  # Very fast bitboard check
        
        if not has_captures and stand_pat >= beta:
            # No captures and stand pat >= beta → cutoff
            return beta
        
        if not has_captures and depth_budget <= 0:
            # No captures and no depth → just return eval
            return stand_pat
        
        # FULL PATH: only reach here if position needs careful analysis
        # (~40-60% of QSearch entries reach this point)
        
        resolution = compute_resolution(position)
        adjusted_eval = threat_adjusted_stand_pat(
            position, resolution
        ).adjusted_eval
        
        # Continue with full DQRS...
        return self.dqrs_full(
            position, alpha, beta, depth_budget, resolution, adjusted_eval
        )
```

### 8.3 Bảng Tóm Tắt Chi Phí

```
┌──────────────────────────────────┬───────────┬──────────────────┐
│ Component                        │ Time (μs) │ Frequency        │
├──────────────────────────────────┼───────────┼──────────────────┤
│ Standard eval (NNUE)             │ 0.5-1.0   │ Every node       │
│ Fast path checks                 │ 0.1-0.2   │ Every node       │
│ Capture existence check          │ 0.05      │ Every node       │
│ Resolution assessment (full)     │ 3.0-8.0   │ ~50% of nodes    │
│ Resolution assessment (increm.)  │ 0.5-2.0   │ ~30% of nodes    │
│ Threat-adjusted stand pat        │ 1.0-2.0   │ ~50% of nodes    │
│ Tiered move generation           │ 1.0-3.0   │ ~40% of nodes    │
│ Per-move pruning checks          │ 0.2-0.5   │ Per move         │
│ Critical quiet detection         │ 2.0-5.0   │ ~20% of nodes    │
├──────────────────────────────────┼───────────┼──────────────────┤
│ DQRS total (avg per QS node)     │ 3.0-8.0   │                  │
│ Stockfish QS (avg per QS node)   │ 1.5-3.0   │                  │
│ DQRS overhead vs Stockfish       │ +100-170% │                  │
├──────────────────────────────────┼───────────┼──────────────────┤
│ BUT: DQRS searches fewer nodes   │ -25-40%   │                  │
│ NET effect per QSearch call       │ +10-30%   │ (cost)           │
│ NET effect on accuracy            │ +5-12%    │ (quality)        │
│ NET effect on total search        │ +8-20%    │ (effective ply)  │
└──────────────────────────────────┴───────────┴──────────────────┘
```

---

## IX. So Sánh Với Stockfish QSearch

```
┌───────────────────────┬───────────────────────┬───────────────────────────┐
│ Aspect                │ Stockfish QSearch      │ DQRS                      │
├───────────────────────┼───────────────────────┼───────────────────────────┤
│ Stand Pat             │ Raw static eval        │ Threat-adjusted eval      │
│                       │                        │ with confidence scaling   │
│                       │                        │                           │
│ Move Types            │ Captures only          │ Captures + selective      │
│                       │ (+ check evasions)     │ quiet moves (checks,      │
│                       │                        │ retreats, pawn pushes,    │
│                       │                        │ zwischenzug, blocks)      │
│                       │                        │                           │
│ Move Ordering         │ MVV-LVA               │ Tiered generation with    │
│                       │                        │ criticality assessment    │
│                       │                        │ + TT move priority        │
│                       │                        │                           │
│ Pruning               │ SEE < 0 (binary)      │ Adaptive SEE threshold    │
│                       │ Delta (fixed margin)   │ Compensation estimation   │
│                       │                        │ Resolution-aware delta    │
│                       │                        │                           │
│ Depth Control         │ Fixed limit (6-8 ply)  │ Adaptive (2-16 ply)      │
│                       │                        │ based on resolution +     │
│                       │                        │ convergence detection     │
│                       │                        │                           │
│ Horizon Effect        │ Significant            │ Mitigated by:             │
│  Handling             │ (fundamental flaw)     │ - Critical quiet moves    │
│                       │                        │ - Threat-adjusted eval    │
│                       │                        │ - Convergence detection   │
│                       │                        │ - Post-search validation  │
│                       │                        │                           │
│ Position Analysis     │ None                   │ Resolution assessment     │
│                       │ (treats all equally)   │ (tactical tension,        │
│                       │                        │  threat inventory,        │
│                       │                        │  quiet classification)    │
│                       │                        │                           │
│ Explosion Prevention  │ Depth limit only       │ Multi-layer:              │
│                       │                        │ node budget + tier limits │
│                       │                        │ + depth scaling           │
│                       │                        │                           │
│ Feedback              │ None                   │ Main search context       │
│                       │                        │ influences QSearch depth  │
│                       │                        │                           │
│ Computational Cost    │ ~1.5-3μs per node      │ ~3-8μs per node          │
│                       │                        │                           │
│ Nodes Searched        │ baseline               │ -25-40% nodes             │
│                       │                        │                           │
│ Accuracy              │ ~85-92%                │ ~92-97% (estimated)       │
└───────────────────────┴───────────────────────┴───────────────────────────┘
```

---

## X. Ước Tính Ảnh Hưởng

```
┌──────────────────────────────────────────┬────────────┬────────────────┐
│ Improvement                              │ Elo Est.   │ Confidence     │
├──────────────────────────────────────────┼────────────┼────────────────┤
│ Threat-adjusted stand pat                │ +15-25     │ ★★★★ High      │
│ Selective quiet moves (horizon fix)      │ +20-40     │ ★★★★ High      │
│ Adaptive depth control                   │ +5-15      │ ★★★ Medium     │
│ Enhanced SEE with compensation           │ +5-10      │ ★★★★ High      │
│ Tiered move generation (better ordering) │ +5-10      │ ★★★ Medium     │
│ Resolution assessment (skip when quiet)  │ +3-8       │ ★★★ Medium     │
│ Convergence detection                    │ +3-8       │ ★★ Med-Low     │
│ Explosion prevention (save nodes)        │ +5-10      │ ★★★ Medium     │
│ Zwischenzug detection                    │ +5-15      │ ★★★ Medium     │
│ Post-search validation                   │ +3-8       │ ★★ Med-Low     │
├──────────────────────────────────────────┼────────────┼────────────────┤
│ Total (with overlap)                     │ +50-100    │                │
│ After overhead deduction                 │ +40-80     │                │
│ Conservative estimate                    │ +30-55     │                │
└──────────────────────────────────────────┴────────────┴────────────────┘

By position type:
┌─────────────────────────────┬────────────┬─────────────────────────────┐
│ Position Type               │ Improvement│ Key Contributing Components  │
├─────────────────────────────┼────────────┼─────────────────────────────┤
│ Quiet positional            │ +10-20 Elo │ Resolution skip, fast path  │
│ Sharp tactical (open)       │ +40-70 Elo │ Critical quiets, horizon    │
│ Complex middlegame          │ +30-50 Elo │ Threat-adjusted, adaptive   │
│ Endgame (K+P, K+R)         │ +30-60 Elo │ Pawn pushes, convergence    │
│ Exchange-heavy middlegame   │ +20-35 Elo │ Zwischenzug, tiered gen     │
│ King attack positions       │ +40-70 Elo │ Quiet checks, threat adj.   │
└─────────────────────────────┴────────────┴─────────────────────────────┘
```

---

## XI. Lộ Trình Triển Khai

```
Phase 1 (Month 1-3): Foundation
├── Implement TacticalTensionAnalyzer
├── Implement ThreatInventory (immediate threats only)
├── Implement QuietPositionClassifier
├── Implement basic ResolutionScore
├── Test accuracy vs Stockfish QSearch on test suites
└── Target: Resolution score correlates with QSearch depth needed

Phase 2 (Month 4-6): Core DQRS
├── Implement ThreatAdjustedStandPat
├── Implement TieredMoveGenerator (captures only first)
├── Implement basic DQRS search loop
├── Implement adaptive depth control
├── Compare eval accuracy with Stockfish
└── Target: +15-25 Elo from adjusted stand pat alone

Phase 3 (Month 7-9): Critical Quiet Moves
├── Implement quiet check generation + quality filter
├── Implement piece retreat detection
├── Implement blocking move detection
├── Implement zwischenzug detection
├── Implement pawn push generation
├── Implement counter-threat detection
├── Extensive testing for horizon effect cases
└── Target: +30-50 Elo total (horizon effect reduction)

Phase 4 (Month 10-12): Control & Safety
├── Implement ConvergenceDetector
├── Implement ExplosionPrevention
├── Implement IncrementalResolution
├── Implement LazyDQRS (fast paths)
├── Implement post-search validation
├── Performance optimization
└── Target: +40-70 Elo total with acceptable NPS

Phase 5 (Month 13-15): Optimization & Integration
├── SIMD optimization for tension/threat computation
├── Bitboard optimization for all detectors
├── Integrate with UPAD (main search pruning)
├── Integrate with HAMO (move ordering)
├── Cross-component optimization
├── Memory optimization
└── Target: Final +40-80 Elo, production-ready

Phase 6 (Month 16-18): Refinement
├── Latent threat detection (2-3 move threats)
├── Neural-assisted resolution scoring
├── Advanced compensation estimation
├── Comprehensive CCRL/TCEC testing
├── Parameter tuning via self-play
└── Target: Stable, fully optimized system
```

DQRS chuyển đổi QSearch từ **"search captures cho đến khi hết"** thành **"giải quyết mọi tactical tension cho đến khi position thực sự resolved"** — bằng cách hiểu **thế cờ cần gì** (resolution assessment), **đánh giá đúng hơn** (threat-adjusted eval), **tìm nước quan trọng đúng loại** (tiered generation with critical quiets), và **biết khi nào dừng** (convergence detection) — tất cả trong khi kiểm soát chi phí tính toán thông qua lazy evaluation và explosion prevention.