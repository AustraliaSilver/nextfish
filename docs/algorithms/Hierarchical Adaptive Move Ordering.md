

# Kiến Trúc Move Ordering Thế Hệ Mới: HAMO (Hierarchical Adaptive Move Ordering)

---

## I. Phân Tích Sâu Vấn Đề Hiện Tại

### 1.1 Move Ordering Trong Stockfish Hiện Tại

```
Thứ tự ưu tiên hiện tại (đơn giản hóa):

Priority 1: TT Move (Hash move)
Priority 2: Good Captures (MVV-LVA, SEE ≥ 0)
Priority 3: Killer Moves (2 per ply)
Priority 4: Counter Move (1 per previous move)
Priority 5: Quiet Moves (sorted by history score)
Priority 6: Bad Captures (SEE < 0)

History Score = butterfly_history[side][from][to]
             + continuation_history[prev_piece][prev_to][piece][to]  
             + pawn_history[pawn_hash_index][piece][to]
             + capture_history[piece][to][captured_type]
```

### 1.2 Vì Sao Move Ordering Quan Trọng Bậc Nhất

```
Alpha-Beta search efficiency phụ thuộc HOÀN TOÀN vào move ordering:

Perfect ordering:  Nodes = O(b^(d/2))     ← lý tưởng
Random ordering:   Nodes = O(b^d)          ← tệ nhất
Stockfish hiện tại: Nodes ≈ O(b^(0.55d))  ← rất tốt nhưng chưa optimal

Với branching factor b ≈ 35, depth d = 20:
- Perfect:  35^10 ≈ 2.7 × 10^15  
- Current:  35^11 ≈ 9.5 × 10^16  (gấp ~35x)
- Random:   35^20 ≈ 3.3 × 10^30  (không khả thi)

→ Cải thiện move ordering 5% ≈ tăng 1 ply depth
→ Tăng 1 ply ≈ +50-80 Elo ở top level
→ Move ordering là leverage point lớn nhất trong engine
```

### 1.3 Phân Tích Chi Tiết Các Hạn Chế

#### A. TT Move

```
Vấn đề:
1. TT move từ search nông hơn có thể không phải best move ở depth hiện tại
2. TT collision → TT move hoàn toàn sai position
3. TT move đã outdated (từ iteration cũ, position đã khác context)
4. Khi không có TT move → mất hoàn toàn priority 1, 
   tất cả phụ thuộc vào MVV-LVA + history

Thống kê (ước tính từ Stockfish logs):
- TT hit rate: ~50-60% 
- TT move là best move: ~65% khi hit
- Vậy chỉ ~35% positions có TT move đúng ở priority 1
- 65% positions bắt đầu từ MVV-LVA/history → ordering kém hơn đáng kể
```

#### B. MVV-LVA (Most Valuable Victim - Least Valuable Attacker)

```
Vấn đề:
1. Chỉ xét giá trị quân, hoàn toàn bỏ qua positional context
   Ví dụ: PxP trên cột mở có thể tốt hơn BxN ở góc bàn cờ
   Nhưng MVV-LVA luôn xếp BxN (victim=3) trên PxP (victim=1)

2. Không phân biệt được recapture bắt buộc vs capture tùy chọn
   RxR khi đang bị bắt lại ngay ≠ RxR khi đối phương không thể bắt lại

3. SEE chỉ sửa chữa phần nào: phân loại good vs bad captures
   nhưng không ranking tốt giữa các good captures

4. Không xét discovered attack, pin exploitation, 
   hay tactical motif đi kèm capture
```

#### C. Killer Moves

```
Vấn đề:
1. Chỉ lưu 2 killers per ply → bỏ qua nhiều pattern tốt
2. Killer từ ply N có thể hoàn toàn irrelevant ở ply N+2
   (vì position đã thay đổi đáng kể)
3. Không xét structural similarity giữa positions
   - Killer "Nf3" có thể tốt ở nhiều positions tương tự
   - Nhưng hệ thống hiện tại chỉ nhớ theo ply, 
     không theo position similarity
4. Killer overwrite: killer mới thay thế killer cũ 
   mà killer cũ có thể vẫn relevant
5. Không phân biệt "killer vì beta cutoff ở ALL node" 
   vs "killer vì beta cutoff ở CUT node" 
   → reliability khác nhau
```

#### D. History Heuristic

```
Vấn đề chi tiết:

1. Score Pollution (Ô nhiễm điểm)
   - History score tích lũy qua TOÀN BỘ search
   - Move "Nf3" có thể tốt ở 1000 positions nhưng tệ ở 500
   - Score = 1000 × bonus - 500 × penalty = positive
   - Nhưng ở position hiện tại, Nf3 có thể tệ
   - Aging (chia 2 periodically) quá thô sơ

2. Granularity Problem (Vấn đề chi tiết)
   - butterfly_history[side][from][to]: chỉ 2 × 64 × 64 = 8192 entries
   - Hàng triệu positions khác nhau map vào cùng entry
   - Nf3 từ position A ≠ Nf3 từ position B, nhưng cùng entry

3. Continuation History hạn chế
   - [prev_piece][prev_to][curr_piece][curr_to]
   - Chỉ xét 1 nước trước → thiếu deeper patterns
   - Ví dụ: 1.e4 e5 2.Nf3 → sau Nf3, Nc6 thường tốt
     Nhưng continuation history chỉ biết "sau Nf3→c3, Nc6→c6"
     Không capture context là "đang trong Italian Game"

4. Update Mechanism
   - Bonus = depth² cho best move
   - Penalty = -depth² cho non-best moves
   - Vấn đề: depth² scale quá nhanh, deeper searches 
     dominate history bất kể relevance
   - Search ở depth 20 ghi bonus 400, 
     search ở depth 5 ghi bonus 25
   - Nhưng search depth 5 có thể gần position hiện tại hơn

5. Cold Start Problem
   - Đầu search mới, history table trống hoặc stale
   - Vài ply đầu ordering gần như random cho quiet moves
   - Critical vì top-of-tree ordering ảnh hưởng lớn nhất
```

#### E. Vấn Đề Hệ Thống

```
1. Rigid Priority System
   - Luôn: TT > Good Captures > Killers > History Quiets
   - Nhưng trong thực tế:
     * Quiet developing move có thể quan trọng hơn edge capture
     * Killer move có thể tốt hơn certain captures
     * Thứ tự tối ưu PHỤ THUỘC position type

2. Không có Learning Mechanism
   - Tất cả parameters hand-tuned (SPSA)
   - Không self-improve qua experience
   - Không adapt theo opponent style
   - Không adapt theo opening/game type

3. Không có Prediction Model
   - Ordering dựa trên backward-looking data (history)
   - Không forward-looking (dự đoán move nào sẽ tốt ở position này)
   - Không dùng position features để predict move quality

4. Missing Semantic Understanding
   - Không hiểu "loại nước đi": developing, prophylactic, 
     attacking, defending, consolidating
   - Không hiểu plan continuity: nước này có phải continuation 
     của plan trước không?
   - Không hiểu threats: nước này đáp trả threat nào?
```

---

## II. HAMO - Hierarchical Adaptive Move Ordering

### 2.1 Tổng Quan Kiến Trúc

```
┌─────────────────────────────────────────────────────────────────┐
│                    HAMO ARCHITECTURE                             │
│         Hierarchical Adaptive Move Ordering                      │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                    Layer 0: INVARIANT                       │  │
│  │              (Luôn đúng, không cần learning)                │  │
│  │  ┌─────────┐  ┌──────────┐  ┌───────────┐  ┌───────────┐ │  │
│  │  │ Legality │  │ Check    │  │ Recapture │  │ Promotion │ │  │
│  │  │ Filter   │  │ Evasion  │  │ Detection │  │ Detection │ │  │
│  │  └─────────┘  └──────────┘  └───────────┘  └───────────┘ │  │
│  └───────────────────────┬────────────────────────────────────┘  │
│                          ▼                                       │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                    Layer 1: TACTICAL                        │  │
│  │              (Pattern-based, fast computation)              │  │
│  │  ┌──────────┐  ┌──────────┐  ┌───────────┐  ┌──────────┐ │  │
│  │  │ Enhanced │  │ Tactical │  │ Threat    │  │ Hanging  │ │  │
│  │  │ SEE+     │  │ Motif    │  │ Response  │  │ Piece    │ │  │
│  │  │          │  │ Detector │  │ Matcher   │  │ Resolver │ │  │
│  │  └──────────┘  └──────────┘  └───────────┘  └──────────┘ │  │
│  └───────────────────────┬────────────────────────────────────┘  │
│                          ▼                                       │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                    Layer 2: STRATEGIC                       │  │
│  │              (Position-aware, medium computation)           │  │
│  │  ┌──────────┐  ┌──────────┐  ┌───────────┐  ┌──────────┐ │  │
│  │  │ Context  │  │ Plan     │  │ Phase     │  │ Structure│ │  │
│  │  │ History  │  │ Contin.  │  │ Aware     │  │ Aware    │ │  │
│  │  │ System   │  │ Tracker  │  │ Ordering  │  │ Ordering │ │  │
│  │  └──────────┘  └──────────┘  └───────────┘  └──────────┘ │  │
│  └───────────────────────┬────────────────────────────────────┘  │
│                          ▼                                       │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                    Layer 3: PREDICTIVE                      │  │
│  │              (Neural prediction, selective use)             │  │
│  │  ┌──────────────────┐  ┌──────────────────────────────┐   │  │
│  │  │ Move Quality     │  │ Position-Move Compatibility   │   │  │
│  │  │ Predictor (MQP)  │  │ Network (PMCN)               │   │  │
│  │  └──────────────────┘  └──────────────────────────────┘   │  │
│  └───────────────────────┬────────────────────────────────────┘  │
│                          ▼                                       │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                    Layer 4: AGGREGATION                     │  │
│  │              (Unified scoring + adaptive weighting)         │  │
│  │  ┌──────────────────────────────────────────────────────┐  │  │
│  │  │           Adaptive Score Fusion Engine                │  │  │
│  │  │   score = Σᵢ wᵢ(context) × layerᵢ_score(move)      │  │  │
│  │  └──────────────────────────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Triết Lý Thiết Kế

```
Nguyên tắc cốt lõi:

1. LAYERED COMPUTATION
   → Layer rẻ chạy trước, layer đắt chạy có điều kiện
   → Mỗi layer có thể đủ tốt để skip layer sau
   → Worst case: chạy tất cả layers (nhưng hiếm)

2. CONTEXT IS KING
   → Cùng một nước đi, ordering khác nhau tùy position type
   → History/killer/counter phải được contextualize

3. PREDICTION > REACTION
   → Dự đoán nước tốt TRƯỚC khi search, không chỉ dựa vào quá khứ
   → Neural component nhỏ nhưng focused

4. UNIFIED SCORING
   → Không phải rigid priority (TT > captures > killers > quiet)
   → Mọi nước đi có một score duy nhất, comparable across types
   → Quiet move CÓ THỂ xếp trên capture nếu context đòi hỏi

5. SELF-IMPROVING
   → Parameters tự adjust qua search feedback
   → Online learning trong mỗi game
   → Offline learning giữa các games
```

---

## III. Chi Tiết Từng Layer

### 3.1 Layer 0: INVARIANT Layer

#### Mục đích
Xử lý logic bất biến, không phụ thuộc context. Rẻ nhất, luôn chạy.

#### Components

```python
class InvariantLayer:
    """Layer 0: Logic bất biến, O(1) per move"""
    
    def score_move(self, position, move):
        score = 0
        
        # 0a. TT Move (vẫn giữ priority cao nhất khi có)
        if move == self.tt_move:
            score += 100000  # Đảm bảo luôn thử đầu tiên
            return score
        
        # 0b. Check Evasion Priority
        if position.is_in_check():
            if move.is_only_legal_move:
                score += 90000  # Forced move
            elif move.blocks_check_with_piece:
                score += 50000 + piece_value(move.piece)
            elif move.captures_checker:
                score += 60000 + piece_value(move.captured)
            elif move.is_king_move:
                score += 40000
        
        # 0c. Promotion
        if move.is_promotion:
            score += 80000 + promotion_piece_value(move.promotion_type)
            if move.promotion_type == QUEEN:
                if move.gives_check:
                    score += 5000  # Queen promotion with check
        
        # 0d. Recapture (bắt lại quân vừa bắt mình)
        if move.to_square == position.last_capture_square:
            score += 30000 + piece_value(move.captured)
        
        return score
```

### 3.2 Layer 1: TACTICAL Layer

#### Mục đích
Đánh giá chiến thuật ngắn hạn. Nhanh hơn search, cung cấp ordering tốt cho captures và tactical quiet moves.

#### Component 1A: Enhanced SEE+ (SEE Plus)

```python
class EnhancedSEE:
    """SEE mở rộng với positional awareness"""
    
    def see_plus(self, position, move):
        # Standard SEE
        base_see = self.standard_see(position, move)
        
        # Enhancement 1: Pin awareness
        # SEE truyền thống có thể kể attacker bị pin là available
        pin_adjustment = self.pin_correction(position, move)
        
        # Enhancement 2: Discovery bonus
        # Nước bắt quân MỞ đường cho discovered attack
        discovery_bonus = self.discovery_potential(position, move)
        
        # Enhancement 3: Removal of defender bonus
        # Bắt quân đang defend target quan trọng
        removal_bonus = self.defender_removal_value(position, move)
        
        # Enhancement 4: Resulting position quality
        # Sau chuỗi SEE, quân ta đứng ở ô tốt hay xấu?
        placement_quality = self.resulting_placement_quality(
            position, move
        )
        
        return (base_see 
                + pin_adjustment 
                + discovery_bonus * 0.3 
                + removal_bonus * 0.25 
                + placement_quality * 0.15)
    
    def pin_correction(self, position, move):
        """Sửa SEE khi attacker/defender bị pin"""
        correction = 0
        
        # Kiểm tra từng attacker trong SEE sequence
        attackers = self.get_see_attackers(position, move.to_square)
        for attacker in attackers:
            if is_pinned(position, attacker):
                if attacker.side == position.side_to_move:
                    # Attacker của ta bị pin → giảm SEE 
                    # (ta mất 1 attacker)
                    correction -= attacker.value
                else:
                    # Defender của đối phương bị pin → tăng SEE
                    # (đối phương mất 1 defender)
                    correction += attacker.value
        
        return correction
    
    def discovery_potential(self, position, move):
        """Đánh giá discovered attack potential sau capture"""
        potential = 0
        
        # Quân di chuyển khỏi ô → mở đường cho quân phía sau
        piece_behind = get_piece_behind(
            position, move.from_square, move.to_square
        )
        
        if piece_behind and piece_behind.side == position.side_to_move:
            # Kiểm tra xem đường mở có attack gì
            targets = get_targets_along_ray(
                position, move.from_square, piece_behind
            )
            for target in targets:
                if target.side != position.side_to_move:
                    if target.value > piece_behind.value:
                        potential += target.value * 0.5
                    else:
                        potential += target.value * 0.3
        
        return potential
    
    def defender_removal_value(self, position, move):
        """Giá trị của việc loại bỏ quân đang phòng thủ"""
        value = 0
        
        if not move.is_capture:
            return 0
        
        captured_piece = position.piece_at(move.to_square)
        
        # Tìm tất cả quân/ô mà captured_piece đang defend
        defended_targets = get_defended_targets(position, captured_piece)
        
        for target in defended_targets:
            if target.is_piece:
                # Sau khi bắt defender, target có hanging không?
                remaining_defenders = count_defenders(
                    position, target.square
                ) - 1  # -1 vì captured_piece bị bắt
                
                remaining_attackers = count_attackers(
                    position, target.square, position.side_to_move
                )
                
                if remaining_attackers > remaining_defenders:
                    # Target trở nên vulnerable!
                    value += target.piece_value * 0.4
            
            elif target.is_key_square:
                # Defender đang kiểm soát ô quan trọng
                value += target.importance * 0.2
        
        return value
    
    def resulting_placement_quality(self, position, move):
        """Đánh giá chất lượng ô đứng sau exchange"""
        # Sau chuỗi SEE, quân cuối cùng ở move.to_square
        last_piece = self.see_last_capturer(position, move)
        
        if last_piece is None:
            return 0
        
        quality = 0
        
        # Centrality bonus
        quality += centrality_bonus(move.to_square, last_piece.type)
        
        # Outpost bonus (for knights/bishops)
        if last_piece.type in [KNIGHT, BISHOP]:
            if is_outpost(position, move.to_square, last_piece.side):
                quality += 50
        
        # Open file bonus (for rooks)
        if last_piece.type == ROOK:
            if is_open_file(position, file_of(move.to_square)):
                quality += 40
            elif is_semi_open_file(position, file_of(move.to_square)):
                quality += 20
        
        return quality
```

#### Component 1B: Tactical Motif Detector

```python
class TacticalMotifDetector:
    """Phát hiện và đánh giá motif chiến thuật cho mỗi nước đi"""
    
    # Precomputed attack tables cho speed
    FORK_PATTERNS = precompute_fork_patterns()
    PIN_PATTERNS = precompute_pin_patterns()
    SKEWER_PATTERNS = precompute_skewer_patterns()
    
    def score_tactical_motifs(self, position, move):
        score = 0
        
        # Simulate move
        result_pos = position.simulate_move(move)  # Lightweight
        
        # 1. Fork Detection
        fork_score = self.detect_fork(result_pos, move)
        score += fork_score
        
        # 2. Pin Creation
        pin_score = self.detect_pin_creation(result_pos, move)
        score += pin_score
        
        # 3. Skewer Creation
        skewer_score = self.detect_skewer(result_pos, move)
        score += skewer_score
        
        # 4. Discovered Attack
        discovery_score = self.detect_discovered_attack(position, move)
        score += discovery_score
        
        # 5. Double Check
        if move.gives_double_check:
            score += 800  # Double check rất mạnh
        
        # 6. Interference / Interposition
        interference_score = self.detect_interference(result_pos, move)
        score += interference_score
        
        # 7. Clearance
        clearance_score = self.detect_clearance(position, move)
        score += clearance_score
        
        return score
    
    def detect_fork(self, result_pos, move):
        """Phát hiện fork sau nước đi"""
        moving_piece = move.piece_type
        to_square = move.to_square
        
        # Lấy tất cả attacks từ ô đến
        attacks = get_attacks_from(result_pos, to_square, moving_piece)
        
        # Đếm targets có giá trị
        valuable_targets = []
        for target_sq in attacks:
            target = result_pos.piece_at(target_sq)
            if (target and target.side != move.side 
                and target.value >= ROOK_VALUE):
                valuable_targets.append(target)
        
        if len(valuable_targets) >= 2:
            # Fork detected!
            # Giá trị = quân có giá trị thấp nhất trong targets
            # (đối phương sẽ cứu quân có giá trị cao hơn)
            min_value = min(t.value for t in valuable_targets)
            
            # Kiểm tra xem fork có "real" không
            # (quân tạo fork có bị bắt ngay không?)
            if not is_safely_capturable(result_pos, to_square):
                return min_value * 0.7  # Strong fork
            elif min_value > piece_value(moving_piece):
                return (min_value - piece_value(moving_piece)) * 0.5
            else:
                return min_value * 0.1  # Weak fork (quân fork bị bắt)
        
        # Semi-fork: attack 1 valuable piece + 1 less valuable
        if (len(valuable_targets) == 1 
            and len(attacks_on_pieces(attacks, result_pos)) >= 2):
            return valuable_targets[0].value * 0.2
        
        return 0
    
    def detect_pin_creation(self, result_pos, move):
        """Phát hiện pin mới tạo ra"""
        if move.piece_type not in [BISHOP, ROOK, QUEEN]:
            return 0
        
        to_square = move.to_square
        
        # Kiểm tra tất cả rays từ to_square
        for ray_dir in get_ray_directions(move.piece_type):
            pieces_on_ray = get_pieces_along_ray(
                result_pos, to_square, ray_dir
            )
            
            if len(pieces_on_ray) >= 2:
                first_piece = pieces_on_ray[0]
                second_piece = pieces_on_ray[1]
                
                # Pin: first piece là enemy, second piece cũng enemy và valuable
                if (first_piece.side != move.side 
                    and second_piece.side != move.side):
                    
                    if second_piece.type == KING:
                        # Absolute pin!
                        return first_piece.value * 0.6
                    elif second_piece.value > first_piece.value:
                        # Relative pin
                        return (second_piece.value - first_piece.value) * 0.3
        
        return 0
    
    def detect_clearance(self, position, move):
        """Phát hiện clearance sacrifice / clearance move"""
        score = 0
        
        # Quân di chuyển MỞ đường cho quân khác
        from_sq = move.from_square
        
        # Kiểm tra từng quân cùng phe
        for friendly_piece in position.pieces(move.side):
            if friendly_piece.square == from_sq:
                continue
            
            # Friendly piece có attack qua from_sq trước khi di chuyển?
            if from_sq in between_squares(
                friendly_piece.square, 
                friendly_piece.ray_targets
            ):
                # Di chuyển mở đường!
                # Đánh giá targets mới cho friendly piece
                new_targets = get_new_targets_after_clearance(
                    position, friendly_piece, from_sq
                )
                for target in new_targets:
                    if target.is_enemy_piece:
                        score += target.value * 0.15
                    elif target.is_key_square:
                        score += target.importance * 0.1
        
        return score
```

#### Component 1C: Threat Response Matcher

```python
class ThreatResponseMatcher:
    """Đánh giá nước đi dựa trên việc nó respond threat nào"""
    
    def score_threat_response(self, position, move):
        """Nước đi có giải quyết mối đe dọa hiện tại không?"""
        
        if not position.has_threats():
            return 0
        
        score = 0
        threats = self.identify_current_threats(position)
        
        # Simulate move
        result_pos = position.simulate_move(move)
        remaining_threats = self.identify_current_threats_for(
            result_pos, position.opponent
        )
        
        # Tính threats được giải quyết
        resolved_threats = set(threats) - set(remaining_threats)
        
        for threat in resolved_threats:
            # Bonus cho việc giải quyết threat
            score += threat.severity * 0.8
            
            # Extra bonus nếu giải quyết bằng cách tạo counter-threat
            if move_creates_counter_threat(result_pos, move):
                score += threat.severity * 0.3  # Counter-attack bonus
        
        # Penalty cho threats KHÔNG giải quyết
        for threat in remaining_threats:
            if threat.is_immediate and threat.severity > 200:
                score -= 100  # Penalty nhẹ: nước này bỏ qua threat lớn
        
        return score
    
    def identify_current_threats(self, position):
        """Xác định tất cả threats đối phương đang tạo"""
        threats = []
        
        opponent = position.opponent
        
        # 1. Capture threats (quân ta bị attack và không đủ defend)
        for our_piece in position.pieces(position.side_to_move):
            attackers = get_attackers(position, our_piece.square, opponent)
            defenders = get_defenders(position, our_piece.square, 
                                     position.side_to_move)
            
            if len(attackers) > len(defenders):
                threat = Threat(
                    type="capture",
                    target=our_piece,
                    severity=our_piece.value,
                    is_immediate=True
                )
                threats.append(threat)
            elif (attackers and 
                  min_attacker_value(attackers) < our_piece.value):
                threat = Threat(
                    type="favorable_exchange",
                    target=our_piece,
                    severity=our_piece.value - min_attacker_value(attackers),
                    is_immediate=True
                )
                threats.append(threat)
        
        # 2. Fork threats (đối phương CÓ THỂ fork ở nước tiếp)
        for enemy_piece in position.pieces(opponent):
            for target_sq in enemy_piece.legal_moves():
                fork_targets = detect_fork_at(
                    position, target_sq, enemy_piece.type, opponent
                )
                if len(fork_targets) >= 2:
                    min_val = min(t.value for t in fork_targets)
                    threat = Threat(
                        type="fork_threat",
                        target=target_sq,
                        severity=min_val * 0.7,
                        is_immediate=False
                    )
                    threats.append(threat)
        
        # 3. Mating threats
        mate_threat_score = evaluate_mate_threat(position, opponent)
        if mate_threat_score > 0:
            threats.append(Threat(
                type="mate_threat",
                severity=mate_threat_score,
                is_immediate=(mate_threat_score > 500)
            ))
        
        # 4. Promotion threats
        for pawn in position.pawns(opponent):
            if pawn.rank >= 6:  # 6th rank or higher
                threats.append(Threat(
                    type="promotion_threat",
                    target=pawn,
                    severity=700 - pawn.rank * 50,
                    is_immediate=(pawn.rank == 7)
                ))
        
        return threats
```

#### Component 1D: Hanging Piece Resolver

```python
class HangingPieceResolver:
    """Ưu tiên nước đi giải quyết quân đang treo"""
    
    def score_hanging_resolution(self, position, move):
        """Nước đi có cứu/exploit quân treo không?"""
        score = 0
        
        # Quân ta đang treo (undefended hoặc under-defended)
        our_hanging = self.find_hanging_pieces(
            position, position.side_to_move
        )
        
        # Quân đối phương đang treo
        their_hanging = self.find_hanging_pieces(
            position, position.opponent
        )
        
        # A. Nước đi bắt quân treo đối phương
        if move.is_capture and move.to_square in their_hanging:
            score += their_hanging[move.to_square].value * 1.5
            # Bonus: bắt quân treo = free material
        
        # B. Nước đi defend quân ta đang treo
        result_pos = position.simulate_move(move)
        our_hanging_after = self.find_hanging_pieces(
            result_pos, position.side_to_move
        )
        
        rescued = set(our_hanging.keys()) - set(our_hanging_after.keys())
        for sq in rescued:
            score += our_hanging[sq].value * 0.6
            # Bonus: cứu quân treo
        
        # C. Nước đi di chuyển quân đang treo
        if move.from_square in our_hanging:
            piece = our_hanging[move.from_square]
            if not is_hanging(result_pos, move.to_square, 
                            position.side_to_move):
                score += piece.value * 0.5
                # Bonus: tự cứu bằng cách di chuyển
        
        # D. Penalty: nước đi tạo quân treo MỚI cho ta
        new_hanging = set(our_hanging_after.keys()) - set(our_hanging.keys())
        for sq in new_hanging:
            score -= our_hanging_after[sq].value * 0.4
        
        return score
```

### 3.3 Layer 2: STRATEGIC Layer

#### Mục đích
Đánh giá move quality dựa trên hiểu biết chiến lược về thế cờ. Tốn hơn Layer 1 nhưng cung cấp thông tin không thể có từ chiến thuật thuần túy.

#### Component 2A: Context History System

```python
class ContextHistorySystem:
    """History heuristic nhận biết context - thay thế hoàn toàn 
    butterfly history + continuation history"""
    
    def __init__(self):
        # === MULTI-DIMENSIONAL HISTORY ===
        
        # Dimension 1: Piece-To (like current, nhưng richer)
        # [side][piece_type][to_square]
        self.piece_to_history = HistoryTable(
            shape=(2, 6, 64), 
            max_value=16384
        )
        
        # Dimension 2: Phase-aware history
        # [phase][piece_type][to_square]
        # phase: 0=opening, 1=early_mid, 2=late_mid, 3=endgame
        self.phase_history = HistoryTable(
            shape=(4, 6, 64),
            max_value=16384
        )
        
        # Dimension 3: King-zone history
        # [our_king_zone][their_king_zone][piece_type][to_zone]
        # zone: 0=kingside, 1=center, 2=queenside
        self.king_zone_history = HistoryTable(
            shape=(3, 3, 6, 3),
            max_value=16384
        )
        
        # Dimension 4: Pawn structure type history
        # [pawn_structure_class][piece_type][to_square]
        # pawn_structure_class: hash of key pawn features → 64 buckets
        self.pawn_structure_history = HistoryTable(
            shape=(64, 6, 64),
            max_value=16384
        )
        
        # Dimension 5: Threat-context history
        # [threat_type][piece_type][to_square]
        # Nước đi tốt khi đối phương đe dọa X
        self.threat_context_history = HistoryTable(
            shape=(8, 6, 64),  # 8 threat types
            max_value=16384
        )
        
        # Dimension 6: Deep continuation history
        # [move_n_2_piece][move_n_2_to][move_n_1_piece][move_n_1_to]
        #   → [curr_piece][curr_to]
        # Xét 2 nước trước thay vì 1
        self.deep_continuation = HistoryTable(
            shape=(6, 64, 6, 64, 6, 64),
            max_value=8192,
            sparse=True  # Sparse storage vì kích thước lớn
        )
        
        # === ADAPTIVE WEIGHTING ===
        # Trọng số mỗi dimension, tự điều chỉnh
        self.dim_weights = {
            'piece_to': 1.0,
            'phase': 0.8,
            'king_zone': 0.6,
            'pawn_structure': 0.7,
            'threat_context': 0.5,
            'deep_continuation': 0.9
        }
        
        # Weight adjustment tracking
        self.prediction_accuracy = {dim: RunningAverage() 
                                    for dim in self.dim_weights}
    
    def get_score(self, position, move, search_context):
        """Tính combined history score"""
        
        side = position.side_to_move
        piece = move.piece_type
        to_sq = move.to_square
        
        scores = {}
        
        # D1: Piece-To
        scores['piece_to'] = self.piece_to_history.get(
            side, piece, to_sq
        )
        
        # D2: Phase
        phase = classify_phase(position)
        scores['phase'] = self.phase_history.get(phase, piece, to_sq)
        
        # D3: King zones
        our_kz = king_zone(position, side)
        their_kz = king_zone(position, 1 - side)
        to_zone = square_zone(to_sq)
        scores['king_zone'] = self.king_zone_history.get(
            our_kz, their_kz, piece, to_zone
        )
        
        # D4: Pawn structure
        ps_class = pawn_structure_class(position)
        scores['pawn_structure'] = self.pawn_structure_history.get(
            ps_class, piece, to_sq
        )
        
        # D5: Threat context
        threat_type = classify_primary_threat(position)
        scores['threat_context'] = self.threat_context_history.get(
            threat_type, piece, to_sq
        )
        
        # D6: Deep continuation
        if search_context.has_move_history(2):
            m2_piece, m2_to = search_context.move_n_minus_2()
            m1_piece, m1_to = search_context.move_n_minus_1()
            scores['deep_continuation'] = self.deep_continuation.get(
                m2_piece, m2_to, m1_piece, m1_to, piece, to_sq
            )
        else:
            scores['deep_continuation'] = 0
        
        # Weighted combination
        total = sum(
            self.dim_weights[dim] * scores[dim] 
            for dim in scores
        )
        
        # Normalize
        total_weight = sum(self.dim_weights.values())
        return total / total_weight
    
    def update(self, position, move, depth, is_best, search_context):
        """Cập nhật tất cả dimensions"""
        
        bonus = compute_bonus(depth)
        
        side = position.side_to_move
        piece = move.piece_type
        to_sq = move.to_square
        phase = classify_phase(position)
        ps_class = pawn_structure_class(position)
        threat_type = classify_primary_threat(position)
        
        if is_best:
            self.piece_to_history.add_bonus(side, piece, to_sq, bonus)
            self.phase_history.add_bonus(phase, piece, to_sq, bonus)
            self.pawn_structure_history.add_bonus(
                ps_class, piece, to_sq, bonus
            )
            self.threat_context_history.add_bonus(
                threat_type, piece, to_sq, bonus
            )
            # ... tương tự cho các dimensions khác
        else:
            penalty = -bonus
            self.piece_to_history.add_bonus(side, piece, to_sq, penalty)
            # ... tương tự
    
    def adapt_weights(self, prediction_result):
        """Tự điều chỉnh trọng số dựa trên accuracy"""
        for dim in self.dim_weights:
            accuracy = self.prediction_accuracy[dim].value
            
            # Dimension dự đoán đúng nhiều → tăng trọng số
            # Dimension dự đoán sai nhiều → giảm trọng số
            if accuracy > 0.6:
                self.dim_weights[dim] *= 1.01
            elif accuracy < 0.4:
                self.dim_weights[dim] *= 0.99
            
            # Clamp
            self.dim_weights[dim] = clamp(
                self.dim_weights[dim], 0.1, 2.0
            )


def compute_bonus(depth):
    """Bonus function cải tiến"""
    # Thay vì depth² đơn giản, dùng bounded bonus
    # Tránh deep searches dominate quá mức
    return min(depth * depth, 400)  # Cap at 400


def classify_phase(position):
    """Phân loại phase chi tiết hơn"""
    material = total_material(position)
    
    if material > 6200:  # Nearly full material
        return OPENING if position.move_count < 15 else EARLY_MIDDLEGAME
    elif material > 3000:
        return LATE_MIDDLEGAME
    else:
        return ENDGAME


def pawn_structure_class(position):
    """Hash cấu trúc tốt thành 1 trong 64 classes"""
    features = 0
    
    # Bit 0-2: Số pawn islands trắng (0-4 → 3 bits)
    features |= min(white_pawn_islands(position), 4)
    
    # Bit 3-5: Số pawn islands đen
    features |= min(black_pawn_islands(position), 4) << 3
    
    # Bit 6: Có locked center không (d4 vs d5 hoặc e4 vs e5)
    features |= has_locked_center(position) << 6
    
    # Bit 7: Có open file ở trung tâm không
    features |= has_central_open_file(position) << 7
    
    # ... thêm features nếu cần, tổng max 6 bits = 64 classes
    
    return features & 0x3F  # 6 bits = 64 classes


def classify_primary_threat(position):
    """Phân loại threat chính mà position đang face"""
    threats = identify_threats(position)
    
    if not threats:
        return THREAT_NONE          # 0
    
    max_threat = max(threats, key=lambda t: t.severity)
    
    if max_threat.type == "mate_threat":
        return THREAT_MATE          # 1
    elif max_threat.type == "capture" and max_threat.severity >= 500:
        return THREAT_MAJOR_CAPTURE # 2
    elif max_threat.type == "capture":
        return THREAT_MINOR_CAPTURE # 3
    elif max_threat.type == "fork_threat":
        return THREAT_FORK          # 4
    elif max_threat.type == "promotion_threat":
        return THREAT_PROMOTION     # 5
    elif max_threat.type == "positional":
        return THREAT_POSITIONAL    # 6
    else:
        return THREAT_OTHER         # 7
```

#### Component 2B: Plan Continuity Tracker

```python
class PlanContinuityTracker:
    """Theo dõi và ưu tiên nước đi tiếp nối kế hoạch"""
    
    # Định nghĩa các "plan templates"
    PLAN_TEMPLATES = {
        'kingside_attack': {
            'indicators': ['piece_aimed_at_kingside', 'pawn_storm_h_g',
                          'piece_sacrifice_on_h_file'],
            'typical_moves': ['h4', 'g4', 'Bxh7', 'Ng5', 'Qh5',
                            'Rh1', 'Rdg1'],
            'piece_types_involved': [QUEEN, BISHOP, KNIGHT, ROOK],
            'target_zone': KINGSIDE,
        },
        'queenside_expansion': {
            'indicators': ['a_b_pawn_push', 'piece_pressure_c_file',
                          'minority_attack'],
            'typical_moves': ['b4', 'a4', 'b5', 'Rb1', 'Qa4'],
            'piece_types_involved': [ROOK, QUEEN, PAWN],
            'target_zone': QUEENSIDE,
        },
        'central_breakthrough': {
            'indicators': ['e_d_pawn_tension', 'piece_pressure_center',
                          'pawn_break_preparation'],
            'typical_moves': ['d4', 'e4', 'd5', 'e5', 'f4', 'c4'],
            'piece_types_involved': [PAWN, KNIGHT, BISHOP],
            'target_zone': CENTER,
        },
        'piece_improvement': {
            'indicators': ['misplaced_piece', 'knight_maneuver',
                          'bishop_repositioning'],
            'typical_moves': ['Nf1-g3', 'Bd3-c2', 'Rf1-e1'],
            'piece_types_involved': [KNIGHT, BISHOP, ROOK],
            'target_zone': None,
        },
        'endgame_technique': {
            'indicators': ['king_centralization', 'passed_pawn_advance',
                          'rook_behind_passer'],
            'typical_moves': ['Kf2-e3', 'a5', 'Rd1-d7'],
            'piece_types_involved': [KING, PAWN, ROOK],
            'target_zone': None,
        },
        'defensive_consolidation': {
            'indicators': ['king_exposed', 'piece_defending_king',
                          'exchange_simplification'],
            'typical_moves': ['Kg1', 'Rf1', 'Be2', 'Qe1'],
            'piece_types_involved': [KING, ROOK, BISHOP, QUEEN],
            'target_zone': None,
        },
    }
    
    def __init__(self):
        self.active_plans = {}  # plan_name → confidence score
        self.move_history = []  # Recent moves made
        self.plan_progress = {} # plan_name → progress score
    
    def update_after_move(self, position, move):
        """Cập nhật plan tracking sau mỗi nước đi"""
        self.move_history.append(move)
        
        for plan_name, template in self.PLAN_TEMPLATES.items():
            # Kiểm tra nước đi có thuộc plan này không
            compatibility = self.check_move_plan_compatibility(
                move, template, position
            )
            
            if compatibility > 0.5:
                # Tăng confidence cho plan này
                if plan_name not in self.active_plans:
                    self.active_plans[plan_name] = 0.0
                self.active_plans[plan_name] = min(
                    self.active_plans[plan_name] + compatibility * 0.3,
                    1.0
                )
                self.plan_progress[plan_name] = (
                    self.plan_progress.get(plan_name, 0) + 1
                )
            else:
                # Giảm confidence nhẹ (plan decay)
                if plan_name in self.active_plans:
                    self.active_plans[plan_name] *= 0.85
    
    def score_plan_continuity(self, position, move):
        """Đánh giá nước đi có tiếp nối plan hiện tại không"""
        if not self.active_plans:
            return 0
        
        total_score = 0
        
        for plan_name, confidence in self.active_plans.items():
            if confidence < 0.2:
                continue  # Plan quá yếu, bỏ qua
            
            template = self.PLAN_TEMPLATES[plan_name]
            compatibility = self.check_move_plan_compatibility(
                move, template, position
            )
            
            # Score = confidence × compatibility × progress_bonus
            progress_bonus = 1.0 + self.plan_progress.get(plan_name, 0) * 0.1
            
            plan_score = confidence * compatibility * progress_bonus
            total_score += plan_score
        
        return total_score * 500  # Scale to centipawn-like range
    
    def check_move_plan_compatibility(self, move, template, position):
        """Kiểm tra nước đi compatible với plan template"""
        score = 0.0
        checks = 0
        
        # 1. Piece type match
        if move.piece_type in template['piece_types_involved']:
            score += 0.3
        checks += 1
        
        # 2. Target zone match
        if template['target_zone']:
            move_zone = square_zone(move.to_square)
            if move_zone == template['target_zone']:
                score += 0.4
            checks += 1
        
        # 3. Move pattern match
        move_san = move.to_san(position)
        for typical in template['typical_moves']:
            if self.fuzzy_move_match(move_san, typical):
                score += 0.3
                break
        checks += 1
        
        # 4. Position indicator match
        matched_indicators = 0
        for indicator in template['indicators']:
            if self.check_indicator(position, indicator, move):
                matched_indicators += 1
        if template['indicators']:
            score += 0.3 * (matched_indicators / len(template['indicators']))
        checks += 1
        
        return score / checks if checks > 0 else 0
    
    def fuzzy_move_match(self, actual_san, template_san):
        """So khớp mờ giữa nước đi thực và template"""
        # Exact match
        if actual_san == template_san:
            return True
        
        # Piece type match + general direction
        if (actual_san[0] == template_san[0] and  # Same piece
            actual_san[-1] == template_san[-1]):    # Same target rank/file
            return True
        
        # Range match (e.g., 'h4' matches template 'h4')
        if len(actual_san) == 2 and len(template_san) == 2:
            if actual_san[0] == template_san[0]:  # Same file
                return abs(int(actual_san[1]) - int(template_san[1])) <= 1
        
        return False
```

#### Component 2C: Phase-Aware Ordering

```python
class PhaseAwareOrdering:
    """Điều chỉnh ordering dựa trên phase cụ thể"""
    
    def score_phase_relevance(self, position, move):
        """Nước đi có phù hợp với phase hiện tại không?"""
        phase = classify_phase_detailed(position)
        score = 0
        
        if phase == OPENING:
            score += self.opening_relevance(position, move)
        elif phase == EARLY_MIDDLEGAME:
            score += self.early_middlegame_relevance(position, move)
        elif phase == LATE_MIDDLEGAME:
            score += self.late_middlegame_relevance(position, move)
        elif phase == ENDGAME:
            score += self.endgame_relevance(position, move)
        
        return score
    
    def opening_relevance(self, position, move):
        score = 0
        
        # Development bonus
        if is_developing_move(position, move):
            score += 200
            if move.piece_type == KNIGHT:
                # Phát triển mã trước tượng (heuristic)
                if not position.has_developed(BISHOP, move.side):
                    score += 50
            
            # Phát triển hướng trung tâm
            if is_center_oriented(move.to_square):
                score += 100
        
        # Castling bonus
        if move.is_castling:
            score += 300  # Cao priority trong opening
        
        # Center pawn push
        if move.piece_type == PAWN:
            if move.to_square in [D4, E4, D5, E5]:
                score += 250
            elif move.to_square in [C4, F4, C5, F5]:
                score += 100
        
        # Penalty: moving same piece twice
        if position.has_moved(move.piece_type, move.from_square):
            score -= 150
        
        # Penalty: early queen development
        if move.piece_type == QUEEN and position.move_count < 10:
            undeveloped = count_undeveloped_minors(position, move.side)
            score -= undeveloped * 100
        
        # Penalty: moving king without castling intent
        if move.piece_type == KING and not move.is_castling:
            if position.can_castle(move.side):
                score -= 200  # Mất quyền nhập thành
        
        return score
    
    def early_middlegame_relevance(self, position, move):
        score = 0
        
        # Piece coordination
        if improves_piece_coordination(position, move):
            score += 150
        
        # Pawn break preparation
        if prepares_pawn_break(position, move):
            score += 200
        
        # Rook to open/semi-open file
        if move.piece_type == ROOK:
            to_file = file_of(move.to_square)
            if is_open_file(position, to_file):
                score += 250
            elif is_semi_open_file(position, to_file):
                score += 150
        
        # King safety moves (prophylactic)
        if move.piece_type == KING and is_prophylactic_king_move(position, move):
            score += 100
        
        return score
    
    def late_middlegame_relevance(self, position, move):
        score = 0
        
        # Exchange management
        # Nếu đang thắng → khuyến khích trao đổi
        eval_score = quick_eval(position)
        if eval_score > 200 and move.is_capture:
            score += 100  # Winning → simplify
        elif eval_score < -200 and move.is_capture:
            score -= 50  # Losing → avoid exchanges
        
        # Passed pawn creation/advancement
        if creates_passed_pawn(position, move):
            score += 300
        if move.piece_type == PAWN and is_passed_pawn(position, move.from_square):
            score += 200 + rank_bonus(move.to_square)
        
        # Piece activity improvement
        activity_gain = piece_activity_delta(position, move)
        score += activity_gain * 2
        
        return score
    
    def endgame_relevance(self, position, move):
        score = 0
        
        # King centralization (crucial in endgame)
        if move.piece_type == KING:
            centrality_gain = (centrality(move.to_square) 
                             - centrality(move.from_square))
            score += centrality_gain * 100
        
        # Passed pawn advancement (highest priority)
        if (move.piece_type == PAWN 
            and is_passed_pawn(position, move.from_square)):
            score += 400 + rank_of(move.to_square) * 80
        
        # Rook activity
        if move.piece_type == ROOK:
            if is_rook_behind_passer(position, move):
                score += 300
            if goes_to_7th_rank(move):
                score += 250
        
        # Opposition (kings facing each other)
        if move.piece_type == KING:
            if gains_opposition(position, move):
                score += 200
        
        # Avoid pointless piece moves (tempo = critical in endgame)
        if move.piece_type in [KNIGHT, BISHOP]:
            if not has_clear_purpose(position, move):
                score -= 100  # Aimless piece move in endgame
        
        return score
```

#### Component 2D: Structure-Aware Ordering

```python
class StructureAwareOrdering:
    """Ordering dựa trên structural considerations"""
    
    def score_structural_impact(self, position, move):
        score = 0
        
        # 1. Pawn structure improvement
        if move.piece_type == PAWN:
            score += self.pawn_move_structural_score(position, move)
        
        # 2. Piece placement relative to structure
        score += self.piece_structure_harmony(position, move)
        
        # 3. Structural vulnerability exploitation
        score += self.exploit_structural_weakness(position, move)
        
        return score
    
    def pawn_move_structural_score(self, position, move):
        score = 0
        
        # Fixing doubled pawns
        if fixes_doubled_pawn(position, move):
            score += 100
        
        # Creating passed pawn
        if creates_passed_pawn(position, move):
            score += 250
        
        # Penalty: creating isolated pawn
        if creates_isolated_pawn(position, move):
            score -= 80
        
        # Penalty: creating backward pawn
        if creates_backward_pawn(position, move):
            score -= 60
        
        # Pawn break value
        if is_pawn_break(position, move):
            # Evaluate break dynamically
            break_value = evaluate_pawn_break(position, move)
            score += break_value
        
        # Pawn chain extension
        if extends_pawn_chain(position, move):
            score += 50
        
        return score
    
    def piece_structure_harmony(self, position, move):
        """Quân đi đến ô hài hòa với cấu trúc tốt"""
        score = 0
        
        # Knight to outpost (ô được tốt protect, không bị tốt đối phương đuổi)
        if move.piece_type == KNIGHT:
            if is_outpost(position, move.to_square, move.side):
                score += 200
                if is_supported_outpost(position, move.to_square, move.side):
                    score += 100  # Extra: outpost được tốt support
        
        # Bishop on good diagonal (không bị chặn bởi tốt mình)
        if move.piece_type == BISHOP:
            pawns_on_color = count_pawns_on_bishop_color(
                position, move.to_square, move.side
            )
            # Ít tốt cùng màu ô = tượng tốt
            score += (4 - pawns_on_color) * 30
            
            # Long diagonal control
            if is_long_diagonal(move.to_square):
                score += 50
        
        # Rook on file matching structure
        if move.piece_type == ROOK:
            file = file_of(move.to_square)
            if is_open_file(position, file):
                score += 200
            elif is_semi_open_file(position, file):
                score += 100
            
            # Rook opposing enemy queen/rook on same file
            if has_enemy_major_on_file(position, file, move.side):
                score += 80
        
        return score
    
    def exploit_structural_weakness(self, position, move):
        """Nước đi khai thác yếu điểm cấu trúc đối phương"""
        score = 0
        opponent = 1 - move.side
        
        # Target isolated pawns
        if move.is_capture or attacks_square(move, get_isolated_pawns(position, opponent)):
            for iso_pawn in get_isolated_pawns(position, opponent):
                if move.to_square == iso_pawn or attacks_square_from(
                    move.to_square, iso_pawn, move.piece_type
                ):
                    score += 80
        
        # Target backward pawns
        for back_pawn in get_backward_pawns(position, opponent):
            if attacks_square_from(
                move.to_square, back_pawn, move.piece_type
            ):
                score += 60
        
        # Piece placement on weak squares
        weak_squares = get_weak_squares(position, opponent)
        if move.to_square in weak_squares:
            score += 120
        
        # Pressure on pawn chain base
        chain_bases = get_pawn_chain_bases(position, opponent)
        for base in chain_bases:
            if attacks_square_from(
                move.to_square, base, move.piece_type
            ):
                score += 100
        
        return score
```

### 3.4 Layer 3: PREDICTIVE Layer

#### Mục đích
Dùng neural network nhỏ chuyên dụng để predict move quality trực tiếp từ position features. Chỉ chạy ở near-root nodes hoặc khi ordering quan trọng.

#### Component 3A: Move Quality Predictor (MQP)

```
┌─────────────────────────────────────────────────────────────┐
│              Move Quality Predictor (MQP)                    │
│                                                              │
│  Design Philosophy:                                          │
│  - Tiny network, fast inference (~2-5μs per move)            │
│  - Specialized for move ordering, NOT evaluation             │
│  - Trained to predict "is this the best move?"               │
│  - Complement to (not replacement for) history heuristic     │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Position Features (96 features)                       │   │
│  │                                                       │   │
│  │ Material (12):                                        │   │
│  │   Per-piece-type count for each side                  │   │
│  │                                                       │   │
│  │ King Safety (16):                                     │   │
│  │   Pawn shield (6), attack units (4),                  │   │
│  │   open files near king (4), castling rights (2)       │   │
│  │                                                       │   │
│  │ Pawn Structure (20):                                  │   │
│  │   Islands (2), passed pawns (4), isolated (2),        │   │
│  │   doubled (2), backward (2), chains (4),              │   │
│  │   tension points (4)                                  │   │
│  │                                                       │   │
│  │ Piece Activity (24):                                  │   │
│  │   Mobility per piece type (12),                       │   │
│  │   centrality (6), coordination (6)                    │   │
│  │                                                       │   │
│  │ Tactical Features (16):                               │   │
│  │   Hanging pieces (4), pins (4),                       │   │
│  │   forks available (4), checks available (4)           │   │
│  │                                                       │   │
│  │ Global (8):                                           │   │
│  │   Phase (1), tempo (1), eval_score (1),               │   │
│  │   eval_confidence (1), move_number (1),               │   │
│  │   time_pressure (1), complexity (1), threat_level (1) │   │
│  └───────────────────────┬──────────────────────────────┘   │
│                          │                                   │
│  ┌───────────────────────┴──────────────────────────────┐   │
│  │ Move Features (32 features per move)                  │   │
│  │                                                       │   │
│  │ Basic (10):                                           │   │
│  │   piece_type (6, one-hot), from_sq (1, normalized),   │   │
│  │   to_sq (1, normalized), is_capture (1),              │   │
│  │   is_promotion (1)                                    │   │
│  │                                                       │   │
│  │ Tactical (8):                                         │   │
│  │   see_value (1), gives_check (1),                     │   │
│  │   creates_fork (1), creates_pin (1),                  │   │
│  │   discovered_attack (1), captures_hanging (1),        │   │
│  │   defends_hanging (1), threatens_mate (1)             │   │
│  │                                                       │   │
│  │ History (6):                                          │   │
│  │   butterfly_history (1, normalized),                  │   │
│  │   continuation_history (1, normalized),               │   │
│  │   is_killer (1), is_counter (1),                      │   │
│  │   is_tt_move (1), pawn_history (1, normalized)        │   │
│  │                                                       │   │
│  │ Structural (8):                                       │   │
│  │   improves_structure (1), to_outpost (1),             │   │
│  │   open_file (1), targets_weakness (1),                │   │
│  │   develops_piece (1), king_safety_impact (1),         │   │
│  │   space_gain (1), plan_continuity (1)                 │   │
│  └───────────────────────┬──────────────────────────────┘   │
│                          │                                   │
│                          ▼                                   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Architecture:                                         │   │
│  │                                                       │   │
│  │ Position Features (96) ──→ Dense(64, ReLU)           │   │
│  │                              │                        │   │
│  │                              ▼                        │   │
│  │                     Position Embedding (64)           │   │
│  │                              │                        │   │
│  │                              │    Move Features (32)  │   │
│  │                              │         │              │   │
│  │                              ▼         ▼              │   │
│  │                     Concatenate (96)                   │   │
│  │                              │                        │   │
│  │                              ▼                        │   │
│  │                     Dense(48, ReLU)                    │   │
│  │                              │                        │   │
│  │                              ▼                        │   │
│  │                     Dense(24, ReLU)                    │   │
│  │                              │                        │   │
│  │                              ▼                        │   │
│  │                     Dense(1, Sigmoid)                  │   │
│  │                              │                        │   │
│  │                              ▼                        │   │
│  │                     P(best_move) ∈ [0, 1]             │   │
│  │                                                       │   │
│  │ Total parameters: ~8,000                              │   │
│  │ Inference time: ~2-5μs per move                       │   │
│  │ Memory: ~32KB                                         │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

```python
class MoveQualityPredictor:
    """Neural network dự đoán chất lượng nước đi"""
    
    def __init__(self):
        self.pos_encoder = Dense(96, 64, activation='relu')
        self.merged_layer = Dense(96, 48, activation='relu')  # 64 + 32
        self.hidden = Dense(48, 24, activation='relu')
        self.output = Dense(24, 1, activation='sigmoid')
        
        # Quantized to int8 for speed
        self.quantized = False
    
    def predict_batch(self, position, moves):
        """Predict quality cho batch of moves (amortize position encoding)"""
        
        # Encode position ONCE
        pos_features = self.extract_position_features(position)
        pos_embedding = self.pos_encoder(pos_features)  # 64 dims
        
        scores = []
        for move in moves:
            # Encode each move
            move_features = self.extract_move_features(position, move)
            
            # Concatenate and forward
            merged = concatenate(pos_embedding, move_features)  # 96 dims
            hidden = self.merged_layer(merged)
            hidden = self.hidden(hidden)
            score = self.output(hidden)
            
            scores.append(score)
        
        return scores
    
    def extract_position_features(self, position):
        """96 features mô tả position"""
        features = np.zeros(96)
        
        # Material (12)
        for piece_type in range(6):
            features[piece_type] = popcount(
                position.pieces(WHITE, piece_type)
            ) / 8.0  # Normalize
            features[6 + piece_type] = popcount(
                position.pieces(BLACK, piece_type)
            ) / 8.0
        
        # King Safety (16)
        features[12:16] = extract_pawn_shield_features(position, WHITE)
        features[16:20] = extract_pawn_shield_features(position, BLACK)
        features[20:24] = extract_king_attack_features(position, WHITE)
        features[24:28] = extract_king_attack_features(position, BLACK)
        
        # Pawn Structure (20)
        features[28:48] = extract_pawn_structure_features(position)
        
        # Piece Activity (24)
        features[48:72] = extract_activity_features(position)
        
        # Tactical (16)
        features[72:88] = extract_tactical_features(position)
        
        # Global (8)
        features[88] = total_material(position) / 8000.0  # Phase proxy
        features[89] = tempo_estimate(position) / 5.0
        features[90] = quick_eval(position) / 1000.0  # Normalized eval
        features[91] = nnue_confidence(position) if available else 0.5
        features[92] = position.move_count / 80.0
        features[93] = time_pressure_factor(position)
        features[94] = tactical_complexity(position)
        features[95] = threat_level(position)
        
        return features
    
    def extract_move_features(self, position, move):
        """32 features cho một nước đi"""
        features = np.zeros(32)
        
        # Basic (10)
        features[move.piece_type] = 1.0  # One-hot piece type
        features[6] = move.from_square / 63.0
        features[7] = move.to_square / 63.0
        features[8] = 1.0 if move.is_capture else 0.0
        features[9] = 1.0 if move.is_promotion else 0.0
        
        # Tactical (8)
        features[10] = see(position, move) / 1000.0  # Normalized
        features[11] = 1.0 if move.gives_check else 0.0
        features[12] = 1.0 if creates_fork_fast(position, move) else 0.0
        features[13] = 1.0 if creates_pin_fast(position, move) else 0.0
        features[14] = 1.0 if has_discovered_attack(position, move) else 0.0
        features[15] = 1.0 if captures_hanging_piece(position, move) else 0.0
        features[16] = 1.0 if defends_hanging_piece(position, move) else 0.0
        features[17] = 1.0 if threatens_mate(position, move) else 0.0
        
        # History (6)
        features[18] = butterfly_history_score(position, move) / 16384.0
        features[19] = continuation_history_score(position, move) / 16384.0
        features[20] = 1.0 if is_killer(move) else 0.0
        features[21] = 1.0 if is_counter_move(move) else 0.0
        features[22] = 1.0 if move == tt_move else 0.0
        features[23] = pawn_history_score(position, move) / 16384.0
        
        # Structural (8)
        features[24] = structural_improvement_score(position, move) / 200.0
        features[25] = 1.0 if is_outpost_move(position, move) else 0.0
        features[26] = 1.0 if to_open_file(position, move) else 0.0
        features[27] = 1.0 if targets_weakness(position, move) else 0.0
        features[28] = 1.0 if is_developing_move(position, move) else 0.0
        features[29] = king_safety_impact(position, move) / 200.0
        features[30] = space_gain(position, move) / 10.0
        features[31] = plan_continuity_score(position, move) / 500.0
        
        return features
```

#### MQP Training Pipeline

```python
class MQPTrainer:
    """Training pipeline cho Move Quality Predictor"""
    
    def generate_training_data(self, engine, num_games=100000):
        """Generate data từ self-play"""
        dataset = []
        
        for game_idx in range(num_games):
            position = starting_position()
            
            while not position.is_game_over():
                # Search to reasonable depth
                search_result = engine.search(position, depth=12)
                
                best_move = search_result.best_move
                all_moves = position.legal_moves()
                
                # Extract features
                pos_features = self.extract_position_features(position)
                
                for move in all_moves:
                    move_features = self.extract_move_features(
                        position, move
                    )
                    
                    # Label: 1 nếu là best move, 0 nếu không
                    is_best = (move == best_move)
                    
                    # Soft label: score-based
                    # Nước gần score với best move → label gần 1
                    if move in search_result.move_scores:
                        score_diff = abs(
                            search_result.move_scores[best_move] 
                            - search_result.move_scores[move]
                        )
                        soft_label = max(0, 1.0 - score_diff / 200.0)
                    else:
                        soft_label = 0.0
                    
                    dataset.append({
                        'pos_features': pos_features,
                        'move_features': move_features,
                        'hard_label': float(is_best),
                        'soft_label': soft_label
                    })
                
                # Play best move
                position.make_move(best_move)
        
        return dataset
    
    def train(self, dataset, epochs=50):
        """Train MQP network"""
        
        # Loss: combination of classification and ranking
        def combined_loss(predictions, hard_labels, soft_labels):
            # Binary cross-entropy cho hard labels
            bce = binary_cross_entropy(predictions, hard_labels)
            
            # MSE cho soft labels (ranking information)
            mse = mean_squared_error(predictions, soft_labels)
            
            # Ranking loss: best move should score higher than others
            ranking = pairwise_ranking_loss(predictions, hard_labels)
            
            return 0.3 * bce + 0.3 * mse + 0.4 * ranking
        
        optimizer = Adam(lr=0.001)
        
        for epoch in range(epochs):
            for batch in dataloader(dataset, batch_size=4096):
                predictions = self.model.forward(
                    batch['pos_features'], 
                    batch['move_features']
                )
                
                loss = combined_loss(
                    predictions, 
                    batch['hard_label'],
                    batch['soft_label']
                )
                
                loss.backward()
                optimizer.step()
            
            # Validate
            val_accuracy = self.validate()
            print(f"Epoch {epoch}: accuracy = {val_accuracy:.3f}")
        
        # Quantize for deployment
        self.model.quantize_to_int8()
```

#### Component 3B: Position-Move Compatibility Network (PMCN)

```python
class PositionMoveCompatibilityNetwork:
    """Network đánh giá 'nước đi này có compatible với thế cờ này không?'
    
    Khác MQP: PMCN focus vào semantic compatibility, không chỉ 'best or not'
    Ví dụ: Nước phòng thủ compatible với thế đang bị tấn công
           Nước tấn công compatible với thế đang có initiative
    """
    
    def __init__(self):
        # Position type encoder
        self.pos_type_encoder = Dense(96, 32, activation='relu')
        
        # Move type encoder  
        self.move_type_encoder = Dense(32, 16, activation='relu')
        
        # Compatibility scorer
        # Dùng bilinear interaction thay vì concatenation
        self.bilinear_weight = Parameter(shape=(32, 16))
        
    def compute_compatibility(self, pos_features, move_features):
        """Bilinear compatibility score"""
        pos_embed = self.pos_type_encoder(pos_features)   # (32,)
        move_embed = self.move_type_encoder(move_features) # (16,)
        
        # Bilinear: pos_embed^T × W × move_embed
        compatibility = pos_embed @ self.bilinear_weight @ move_embed
        
        return sigmoid(compatibility)
    
    # Training: supervised từ GM games
    # Label: nước GM chọn = 1.0, các nước khác theo distance từ GM choice
```

### 3.5 Layer 4: AGGREGATION Layer

#### Adaptive Score Fusion Engine

```python
class AdaptiveScoreFusionEngine:
    """Tổng hợp scores từ tất cả layers thành 1 unified score"""
    
    def __init__(self):
        # Base weights cho mỗi layer component
        self.weights = {
            # Layer 0 (invariant)
            'tt_move': 100000,        # Fixed, không thay đổi
            'check_evasion': 90000,   # Fixed
            'promotion': 80000,       # Fixed
            'recapture': 30000,       # Fixed
            
            # Layer 1 (tactical)
            'see_plus': 1.0,
            'tactical_motif': 0.8,
            'threat_response': 0.7,
            'hanging_resolver': 0.9,
            
            # Layer 2 (strategic)
            'context_history': 1.0,
            'plan_continuity': 0.6,
            'phase_relevance': 0.5,
            'structure_impact': 0.5,
            
            # Layer 3 (predictive)
            'mqp_score': 0.7,
            'pmcn_score': 0.4,
        }
        
        # Context-dependent weight modifiers
        self.context_modifiers = {
            'tactical_position': {
                'see_plus': 1.5,
                'tactical_motif': 1.5,
                'threat_response': 1.3,
                'hanging_resolver': 1.3,
                'plan_continuity': 0.3,
                'structure_impact': 0.3,
                'phase_relevance': 0.3,
            },
            'positional_position': {
                'see_plus': 0.8,
                'tactical_motif': 0.5,
                'plan_continuity': 1.5,
                'structure_impact': 1.5,
                'phase_relevance': 1.2,
                'context_history': 1.3,
            },
            'endgame_position': {
                'phase_relevance': 1.8,
                'structure_impact': 1.3,
                'plan_continuity': 0.4,
                'tactical_motif': 0.6,
            },
            'time_pressure': {
                # Giảm components đắt, tăng components rẻ
                'mqp_score': 0.0,      # Skip neural
                'pmcn_score': 0.0,     # Skip neural
                'plan_continuity': 0.2, # Skip plan tracking
                'context_history': 1.5, # Rely more on history
                'see_plus': 0.7,       # Use basic SEE
            },
        }
    
    def compute_unified_score(self, position, move, search_context):
        """Tính final unified score cho một nước đi"""
        
        # Determine context
        context = self.classify_context(position, search_context)
        modifiers = self.get_modifiers(context)
        
        # Determine computation budget
        budget = self.get_computation_budget(search_context)
        
        scores = {}
        
        # === Layer 0: Always compute (O(1)) ===
        scores['invariant'] = self.layer0.score_move(position, move)
        
        if scores['invariant'] >= 50000:
            # TT move, check evasion, promotion → skip other layers
            return scores['invariant']
        
        # === Layer 1: Tactical (compute based on budget) ===
        if budget >= BUDGET_MINIMAL:
            scores['see_plus'] = self.layer1.see_plus.see_plus(
                position, move
            ) * self.get_weight('see_plus', modifiers)
            
            scores['hanging'] = self.layer1.hanging.score_hanging_resolution(
                position, move
            ) * self.get_weight('hanging_resolver', modifiers)
        
        if budget >= BUDGET_NORMAL:
            scores['tactical_motif'] = (
                self.layer1.tactical_motif.score_tactical_motifs(
                    position, move
                ) * self.get_weight('tactical_motif', modifiers)
            )
            
            scores['threat_response'] = (
                self.layer1.threat_response.score_threat_response(
                    position, move
                ) * self.get_weight('threat_response', modifiers)
            )
        
        # === Layer 2: Strategic (compute at deeper nodes) ===
        if budget >= BUDGET_NORMAL:
            scores['context_history'] = (
                self.layer2.context_history.get_score(
                    position, move, search_context
                ) * self.get_weight('context_history', modifiers)
            )
            
            scores['phase'] = (
                self.layer2.phase_aware.score_phase_relevance(
                    position, move
                ) * self.get_weight('phase_relevance', modifiers)
            )
        
        if budget >= BUDGET_EXTENDED:
            scores['plan'] = (
                self.layer2.plan_tracker.score_plan_continuity(
                    position, move
                ) * self.get_weight('plan_continuity', modifiers)
            )
            
            scores['structure'] = (
                self.layer2.structure_aware.score_structural_impact(
                    position, move
                ) * self.get_weight('structure_impact', modifiers)
            )
        
        # === Layer 3: Predictive (only at near-root) ===
        if budget >= BUDGET_FULL and search_context.depth_from_root <= 4:
            scores['mqp'] = (
                self.layer3.mqp.predict(position, move) 
                * 2000  # Scale to centipawn range
                * self.get_weight('mqp_score', modifiers)
            )
            
            scores['pmcn'] = (
                self.layer3.pmcn.compute_compatibility(
                    position, move
                ) * 1000
                * self.get_weight('pmcn_score', modifiers)
            )
        
        # === Aggregate ===
        total = sum(scores.values())
        
        return total
    
    def get_computation_budget(self, search_context):
        """Determine how much computation to spend on ordering"""
        
        # Near root → more budget (ordering matters most)
        if search_context.depth_from_root <= 2:
            return BUDGET_FULL
        elif search_context.depth_from_root <= 5:
            return BUDGET_EXTENDED
        elif search_context.depth_from_root <= 10:
            return BUDGET_NORMAL
        else:
            return BUDGET_MINIMAL
        
        # Adjust for time pressure
        if search_context.time_pressure > 0.8:
            return max(BUDGET_MINIMAL, budget - 1)
        
        # Adjust for PV node (ordering most important)
        if search_context.is_pv_node:
            return min(BUDGET_FULL, budget + 1)
    
    def classify_context(self, position, search_context):
        """Phân loại context để chọn weight modifiers"""
        tactical_score = compute_tactical_intensity(position)
        
        if search_context.time_pressure > 0.8:
            return 'time_pressure'
        elif tactical_score > 0.7:
            return 'tactical_position'
        elif is_endgame(position):
            return 'endgame_position'
        else:
            return 'positional_position'
    
    def get_weight(self, component, modifiers):
        """Lấy modified weight"""
        base = self.weights[component]
        modifier = modifiers.get(component, 1.0)
        return base * modifier
```

---

## IV. Killer Move System Nâng Cấp

```python
class EnhancedKillerSystem:
    """Thay thế hoàn toàn killer move system cũ"""
    
    def __init__(self):
        # Standard killers (mở rộng từ 2 → 4 per ply)
        self.ply_killers = defaultdict(lambda: deque(maxlen=4))
        
        # Threat-response killers
        # "Khi bị đe dọa kiểu X, nước Y thường tốt"
        self.threat_killers = defaultdict(lambda: deque(maxlen=3))
        
        # Piece-type killers
        # "Mã thường nhảy đến ô tốt trong thế tương tự"
        self.piece_killers = defaultdict(lambda: deque(maxlen=3))
        
        # Position-feature killers (mới hoàn toàn)
        # Hash position features → killer
        # Giải quyết vấn đề "killer từ sibling không relevant"
        self.feature_killers = LRUCache(maxsize=4096)
        
        # Refutation table (mới)
        # "Nước đi X ở position tương tự bị refute bởi Y"
        self.refutation_table = defaultdict(lambda: None)
        
        # Counter-move nâng cấp
        # [prev_piece][prev_to][curr_piece_type] → best counter
        self.counter_moves = {}
    
    def get_killers(self, position, ply, search_context):
        """Lấy danh sách killer moves có ranking"""
        candidates = []
        
        # 1. Ply killers (standard)
        for killer in self.ply_killers[ply]:
            if is_legal(position, killer):
                candidates.append((killer, 3000, 'ply_killer'))
        
        # 2. Feature-based killers
        feature_hash = self.compute_feature_hash(position)
        if feature_hash in self.feature_killers:
            for killer in self.feature_killers[feature_hash]:
                if is_legal(position, killer):
                    candidates.append((killer, 2500, 'feature_killer'))
        
        # 3. Threat-response killers
        current_threats = classify_threats(position)
        for threat_type in current_threats:
            for killer in self.threat_killers[threat_type]:
                if is_legal(position, killer):
                    candidates.append((killer, 2000, 'threat_killer'))
        
        # 4. Piece-type killers
        for piece_type in active_piece_types(position):
            for killer in self.piece_killers[piece_type]:
                if is_legal(position, killer):
                    candidates.append((killer, 1500, 'piece_killer'))
        
        # 5. Counter moves
        if search_context.has_previous_move():
            prev = search_context.previous_move
            key = (prev.piece_type, prev.to_square)
            if key in self.counter_moves:
                counter = self.counter_moves[key]
                if is_legal(position, counter):
                    candidates.append((counter, 2000, 'counter_move'))
        
        # 6. Refutation moves
        if search_context.current_move:
            ref = self.refutation_table.get(
                (search_context.current_move.piece_type,
                 search_context.current_move.to_square)
            )
            if ref and is_legal(position, ref):
                candidates.append((ref, 2800, 'refutation'))
        
        # Deduplicate and sort
        seen = set()
        result = []
        for move, score, source in sorted(
            candidates, key=lambda x: -x[1]
        ):
            if move not in seen:
                seen.add(move)
                result.append((move, score))
        
        return result
    
    def update(self, position, move, ply, search_context, is_beta_cutoff):
        """Cập nhật killer system sau beta cutoff"""
        if not is_beta_cutoff:
            return
        
        if move.is_capture:
            return  # Killers chỉ cho quiet moves
        
        # 1. Ply killer
        if move not in self.ply_killers[ply]:
            self.ply_killers[ply].appendleft(move)
        
        # 2. Feature killer
        feature_hash = self.compute_feature_hash(position)
        if feature_hash not in self.feature_killers:
            self.feature_killers[feature_hash] = deque(maxlen=3)
        if move not in self.feature_killers[feature_hash]:
            self.feature_killers[feature_hash].appendleft(move)
        
        # 3. Threat-response killer
        current_threats = classify_threats(position)
        for threat_type in current_threats:
            if move not in self.threat_killers[threat_type]:
                self.threat_killers[threat_type].appendleft(move)
        
        # 4. Piece-type killer
        if move not in self.piece_killers[move.piece_type]:
            self.piece_killers[move.piece_type].appendleft(move)
        
        # 5. Counter move update
        if search_context.has_previous_move():
            prev = search_context.previous_move
            key = (prev.piece_type, prev.to_square)
            self.counter_moves[key] = move
        
        # 6. Refutation update
        if search_context.current_move:
            key = (search_context.current_move.piece_type,
                   search_context.current_move.to_square)
            self.refutation_table[key] = move
    
    def compute_feature_hash(self, position):
        """Hash vị trí dựa trên features quan trọng (không phải Zobrist)
        
        Mục đích: Positions KHÁC Zobrist hash nhưng TƯƠNG TỰ về features
        → có chung killers
        """
        h = 0
        
        # Hash dựa trên piece placement tổng quát
        h ^= piece_placement_hash(position)  # Coarse placement
        
        # Hash dựa trên king positions
        h ^= king_position_hash(position)
        
        # Hash dựa trên pawn structure class
        h ^= pawn_structure_class(position) << 16
        
        # Hash dựa trên tactical features
        h ^= tactical_feature_hash(position) << 24
        
        return h & 0xFFF  # 12 bits = 4096 buckets
```

---

## V. Online Learning System

```python
class OnlineLearningSystem:
    """Tự cải thiện move ordering trong khi chơi"""
    
    def __init__(self, hamo):
        self.hamo = hamo
        
        # Track ordering accuracy
        self.ordering_stats = {
            'total_nodes': 0,
            'best_move_was_first': 0,
            'best_move_was_top3': 0,
            'beta_cutoff_at_move_1': 0,
            'total_beta_cutoffs': 0,
        }
        
        # Per-component accuracy tracking
        self.component_accuracy = defaultdict(lambda: {
            'predicted_high_was_good': 0,
            'predicted_low_was_good': 0,
            'total_predictions': 0,
        })
        
        # Learning rate
        self.learning_rate = 0.01
    
    def on_search_complete(self, node, best_move, ordered_moves):
        """Callback sau khi search một node xong"""
        
        self.ordering_stats['total_nodes'] += 1
        
        # Track ordering quality
        best_idx = ordered_moves.index(best_move) if best_move in ordered_moves else -1
        
        if best_idx == 0:
            self.ordering_stats['best_move_was_first'] += 1
        if best_idx <= 2:
            self.ordering_stats['best_move_was_top3'] += 1
        
        # Per-component feedback
        for component_name in self.hamo.active_components:
            component_score_for_best = self.hamo.get_component_score(
                component_name, node.position, best_move
            )
            
            avg_component_score = np.mean([
                self.hamo.get_component_score(
                    component_name, node.position, m
                ) for m in ordered_moves
            ])
            
            if component_score_for_best > avg_component_score:
                self.component_accuracy[component_name][
                    'predicted_high_was_good'
                ] += 1
            else:
                self.component_accuracy[component_name][
                    'predicted_low_was_good'
                ] += 1
            
            self.component_accuracy[component_name][
                'total_predictions'
            ] += 1
    
    def adapt_weights_periodically(self):
        """Điều chỉnh weights mỗi N nodes"""
        
        for component_name in self.hamo.aggregator.weights:
            stats = self.component_accuracy[component_name]
            
            if stats['total_predictions'] < 100:
                continue  # Chưa đủ data
            
            accuracy = (stats['predicted_high_was_good'] / 
                       stats['total_predictions'])
            
            # Accuracy > 0.5 → component hữu ích, tăng weight
            # Accuracy < 0.5 → component misleading, giảm weight
            adjustment = (accuracy - 0.5) * self.learning_rate
            
            current_weight = self.hamo.aggregator.weights[component_name]
            new_weight = current_weight * (1 + adjustment)
            
            # Clamp
            self.hamo.aggregator.weights[component_name] = clamp(
                new_weight, 0.05, 3.0
            )
            
            # Reset stats
            stats['predicted_high_was_good'] = 0
            stats['predicted_low_was_good'] = 0
            stats['total_predictions'] = 0
    
    def get_ordering_quality_report(self):
        """Báo cáo chất lượng ordering"""
        total = self.ordering_stats['total_nodes']
        if total == 0:
            return "No data"
        
        return {
            'first_move_accuracy': (
                self.ordering_stats['best_move_was_first'] / total
            ),
            'top3_accuracy': (
                self.ordering_stats['best_move_was_top3'] / total
            ),
            'beta_cutoff_efficiency': (
                self.ordering_stats['beta_cutoff_at_move_1'] / 
                max(self.ordering_stats['total_beta_cutoffs'], 1)
            ),
            'component_weights': dict(self.hamo.aggregator.weights),
        }
```

---

## VI. Tối Ưu Hóa Hiệu Năng

### 6.1 Computational Budget per Depth

```
┌──────────────┬───────────────────────────────────────────────────┐
│ Depth from   │ Components Active                                 │
│ Root         │                                                   │
├──────────────┼───────────────────────────────────────────────────┤
│ 0-2          │ ALL layers including Neural (Layer 3)             │
│ (Root area)  │ Full budget: ~50-100μs per move                   │
│              │ Worth it: ordering here saves millions of nodes    │
├──────────────┼───────────────────────────────────────────────────┤
│ 3-5          │ Layer 0 + Layer 1 + Layer 2 (no neural)          │
│ (Near root)  │ Medium budget: ~10-30μs per move                  │
│              │ Strategic components still valuable                │
├──────────────┼───────────────────────────────────────────────────┤
│ 6-10         │ Layer 0 + Layer 1 + Context History only         │
│ (Mid tree)   │ Light budget: ~3-10μs per move                    │
│              │ Tactical + history sufficient                      │
├──────────────┼───────────────────────────────────────────────────┤
│ 11-15        │ Layer 0 + SEE + basic History                    │
│ (Deep tree)  │ Minimal budget: ~1-3μs per move                   │
│              │ Similar to current Stockfish                       │
├──────────────┼───────────────────────────────────────────────────┤
│ 16+          │ Layer 0 + MVV-LVA only                           │
│ (Very deep)  │ Ultra-minimal: <1μs per move                      │
│              │ Speed critical, ordering less impactful            │
├──────────────┼───────────────────────────────────────────────────┤
│ QSearch      │ Layer 0 + basic SEE                               │
│              │ Ultra-minimal: <0.5μs per capture                  │
│              │ Only captures, ordering simple                     │
└──────────────┴───────────────────────────────────────────────────┘
```

### 6.2 Lazy Component Evaluation

```python
class LazyMoveOrdering:
    """Đánh giá moves lazily - chỉ tính score khi cần"""
    
    def __init__(self, position, search_context):
        self.position = position
        self.context = search_context
        self.moves = generate_all_moves(position)
        self.current_phase = 0
        self.scored_moves = []
        self.yielded_count = 0
    
    def next_move(self):
        """Generator: trả về nước tốt nhất tiếp theo"""
        
        # Phase 0: TT move (không cần score gì cả)
        if self.current_phase == 0:
            self.current_phase = 1
            if self.tt_move and self.tt_move in self.moves:
                self.yielded_count += 1
                return self.tt_move
        
        # Phase 1: Score và yield captures
        if self.current_phase == 1:
            if not self.captures_scored:
                self.score_captures()  # Chỉ score captures
                self.captures_scored = True
            
            best_capture = self.pop_best_capture()
            if best_capture:
                self.yielded_count += 1
                return best_capture
            else:
                self.current_phase = 2
        
        # Phase 2: Killers
        if self.current_phase == 2:
            killer = self.next_killer()
            if killer:
                self.yielded_count += 1
                return killer
            else:
                self.current_phase = 3
        
        # Phase 3: Score và yield quiet moves
        if self.current_phase == 3:
            if not self.quiets_scored:
                # Chỉ bây giờ mới score quiet moves
                # Nếu beta cutoff đã xảy ra ở phase 0-2,
                # quiet moves KHÔNG BAO GIỜ bị score → save computation
                self.score_quiet_moves()
                self.quiets_scored = True
            
            best_quiet = self.pop_best_quiet()
            if best_quiet:
                self.yielded_count += 1
                return best_quiet
            else:
                self.current_phase = 4
        
        # Phase 4: Bad captures (SEE < 0)
        if self.current_phase == 4:
            bad_capture = self.pop_next_bad_capture()
            if bad_capture:
                self.yielded_count += 1
                return bad_capture
        
        return None  # No more moves
    
    def score_captures(self):
        """Score chỉ captures - nhanh"""
        self.good_captures = []
        self.bad_captures = []
        
        for move in self.moves:
            if not move.is_capture:
                continue
            
            score = self.hamo.score_capture(self.position, move, self.context)
            
            if see(self.position, move) >= 0:
                self.good_captures.append((move, score))
            else:
                self.bad_captures.append((move, score))
        
        self.good_captures.sort(key=lambda x: -x[1])
    
    def score_quiet_moves(self):
        """Score quiet moves - có thể tốn hơn nhưng chỉ chạy khi cần"""
        self.quiet_moves = []
        
        for move in self.moves:
            if move.is_capture or move == self.tt_move:
                continue
            if move in self.yielded_killers:
                continue
            
            score = self.hamo.score_quiet(self.position, move, self.context)
            self.quiet_moves.append((move, score))
        
        self.quiet_moves.sort(key=lambda x: -x[1])
```

### 6.3 SIMD Batch Scoring

```python
class SIMDBatchScorer:
    """Score nhiều moves cùng lúc dùng SIMD"""
    
    def score_captures_batch(self, position, captures):
        """Score batch captures dùng SIMD operations"""
        n = len(captures)
        
        # Vectorize: tính tất cả features cùng lúc
        victim_values = np.array([
            piece_value(position.piece_at(m.to_square)) 
            for m in captures
        ])
        
        attacker_values = np.array([
            piece_value(m.piece_type) 
            for m in captures
        ])
        
        see_values = np.array([
            see(position, m) for m in captures
        ])
        
        # MVV-LVA scores (vectorized)
        mvv_lva_scores = victim_values * 8 - attacker_values
        
        # SEE bonus/penalty (vectorized)
        see_adjustments = np.where(
            see_values >= 0, 
            see_values * 0.5,    # Good capture bonus
            see_values * 1.0     # Bad capture penalty
        )
        
        # History scores (vectorized lookup)
        history_scores = np.array([
            self.context_history.get_capture_score(position, m)
            for m in captures
        ])
        
        # Combine (all vectorized)
        total_scores = (mvv_lva_scores * 100 
                       + see_adjustments 
                       + history_scores * 0.1)
        
        return total_scores
```

### 6.4 Incremental Feature Computation

```python
class IncrementalFeatures:
    """Tính features incrementally khi make/unmake move"""
    
    def __init__(self):
        self.cached_features = {}
    
    def on_make_move(self, position, move):
        """Cập nhật features sau make move"""
        
        # Chỉ cập nhật features BỊ ẢNH HƯỞNG bởi move
        
        if move.piece_type == PAWN or move.is_capture_of_pawn:
            # Pawn structure changed → update pawn features
            self.update_pawn_features(position, move)
        
        if move.is_capture:
            # Material changed → update material features
            self.update_material_features(position, move)
        
        # Piece mobility: chỉ update pieces affected
        affected_squares = get_affected_squares(move)
        for sq in affected_squares:
            piece = position.piece_at(sq)
            if piece:
                self.update_piece_mobility(position, piece)
        
        # King safety: chỉ update nếu move gần vua
        if (distance(move.to_square, position.king_square(WHITE)) <= 3 or
            distance(move.to_square, position.king_square(BLACK)) <= 3):
            self.update_king_safety_features(position)
        
        # Tactical features: always recompute (fast with bitboards)
        self.update_tactical_features(position)
    
    def on_unmake_move(self, position, move):
        """Restore features sau unmake"""
        # Pop from stack
        self.pop_feature_state()
```

---

## VII. Ước Tính Ảnh Hưởng Toàn Diện

### 7.1 Performance Analysis

```
┌─────────────────────────────────┬───────────┬──────────────────────┐
│ Metric                          │ Stockfish │ HAMO (Estimated)     │
│                                 │ Current   │                      │
├─────────────────────────────────┼───────────┼──────────────────────┤
│ Best move ranked #1             │ ~60%      │ ~72-78%              │
│ Best move in top 3              │ ~82%      │ ~90-94%              │
│ Beta cutoff at move 1 (CUT)    │ ~85%      │ ~90-93%              │
│ Average moves before cutoff    │ ~1.8      │ ~1.3-1.5             │
│ Effective branching factor     │ ~1.8-2.2  │ ~1.5-1.8             │
│ Ordering overhead per node     │ ~2-5μs    │ ~3-15μs (depth dep.) │
│ Net nodes per second           │ ~2M nps   │ ~1.5M nps (-25%)     │
│ Effective depth at same time   │ depth D   │ depth D+1 to D+2     │
│ Estimated Elo gain             │ baseline  │ +40-80 Elo           │
└─────────────────────────────────┴───────────┴──────────────────────┘

Key insight:
- HAMO chậm hơn 25% per node do computation overhead
- NHƯNG tìm beta cutoff nhanh hơn → search ít nodes hơn ~40-60%
- Net effect: tìm được depth sâu hơn 1-2 ply trong cùng thời gian
- 1 ply ≈ +50-80 Elo → total gain +40-80 Elo

Ở near-root (depth 0-5):
- HAMO overhead lớn hơn nhưng impact cũng lớn nhất
- Ordering ở root ảnh hưởng đến TOÀN BỘ search tree

Ở deep tree (depth 15+):
- HAMO fallback về mode đơn giản ≈ current Stockfish
- Minimal overhead, ordering ít quan trọng hơn
```

### 7.2 Breakdown Elo Contribution

```
┌──────────────────────────────────┬───────────┬────────────────────┐
│ Component                        │ Elo Est.  │ Computational Cost │
├──────────────────────────────────┼───────────┼────────────────────┤
│ Enhanced SEE+ (pin, discovery)   │ +5-10     │ +30% vs basic SEE  │
│ Tactical Motif Detector          │ +8-15     │ ~5μs per move      │
│ Threat Response Matcher          │ +5-10     │ ~3μs per move      │
│ Hanging Piece Resolver           │ +3-5      │ ~2μs per move      │
│ Context History System           │ +10-20    │ ~1μs per lookup    │
│ Plan Continuity Tracker          │ +3-8      │ ~4μs per move      │
│ Phase-Aware Ordering             │ +3-8      │ ~2μs per move      │
│ Structure-Aware Ordering         │ +3-5      │ ~3μs per move      │
│ Move Quality Predictor (Neural)  │ +5-15     │ ~5μs per move      │
│ Enhanced Killer System           │ +5-10     │ ~1μs per lookup    │
│ Adaptive Score Fusion            │ +3-5      │ ~1μs per move      │
│ Online Learning                  │ +2-5      │ Periodic, amortized│
├──────────────────────────────────┼───────────┼────────────────────┤
│ TOTAL (with overlap)             │ +40-80    │                    │
│ After overhead deduction         │ +30-65    │                    │
└──────────────────────────────────┴───────────┴────────────────────┘
```

### 7.3 Impact by Position Type

```
┌───────────────────────┬──────────────┬──────────────────────────┐
│ Position Type         │ Improvement  │ Key Contributing Components│
├───────────────────────┼──────────────┼──────────────────────────┤
│ Quiet Positional      │ +50-100 Elo  │ Plan Continuity,         │
│                       │              │ Structure-Aware,          │
│                       │              │ Phase-Aware, PMCN         │
├───────────────────────┼──────────────┼──────────────────────────┤
│ Sharp Tactical        │ +20-40 Elo   │ Tactical Motif,          │
│                       │              │ Enhanced SEE+,            │
│                       │              │ Threat Response            │
├───────────────────────┼──────────────┼──────────────────────────┤
│ Complex Middlegame    │ +40-70 Elo   │ Context History,         │
│                       │              │ MQP Neural,               │
│                       │              │ Killer System              │
├───────────────────────┼──────────────┼──────────────────────────┤
│ Technical Endgame     │ +30-50 Elo   │ Phase-Aware,             │
│                       │              │ Structure-Aware,          │
│                       │              │ Plan Continuity            │
├───────────────────────┼──────────────┼──────────────────────────┤
│ Opening/Theory        │ +10-20 Elo   │ Phase-Aware (opening),   │
│                       │              │ Context History            │
└───────────────────────┴──────────────┴──────────────────────────┘

Nhận xét quan trọng:
- Cải thiện LỚN NHẤT ở thế positional (nơi current ordering yếu nhất)
- Cải thiện VỪA ở thế tactical (current ordering đã khá tốt với SEE)
- Thế complex middlegame: HAMO leverage nhiều components cùng lúc
```

---

## VIII. So Sánh Chi Tiết Với Stockfish

```
┌────────────────────┬─────────────────────────┬──────────────────────────┐
│ Aspect             │ Stockfish Current        │ HAMO                     │
├────────────────────┼─────────────────────────┼──────────────────────────┤
│ Architecture       │ Flat priority list       │ Hierarchical 4 layers    │
│                    │                          │                          │
│ Capture ordering   │ MVV-LVA + SEE           │ SEE+ (pin, discovery,    │
│                    │                          │ defender removal, etc.)   │
│                    │                          │                          │
│ Quiet ordering     │ butterfly + continuation │ 6-dimensional context    │
│                    │ history (2D tables)      │ history with adaptive    │
│                    │                          │ weights                  │
│                    │                          │                          │
│ Killer system      │ 2 killers per ply        │ Multi-source: ply,       │
│                    │ + 1 counter move         │ feature, threat, piece,  │
│                    │                          │ refutation, counter      │
│                    │                          │                          │
│ Strategic awareness│ None (pure tactical)     │ Phase-aware, structure-  │
│                    │                          │ aware, plan continuity   │
│                    │                          │                          │
│ Prediction         │ None (backward only)     │ MQP neural network       │
│                    │                          │ + PMCN compatibility     │
│                    │                          │                          │
│ Adaptation         │ SPSA offline tuning      │ Online learning          │
│                    │                          │ + offline training       │
│                    │                          │                          │
│ Score system       │ Rigid categories         │ Unified continuous score │
│                    │ (captures vs quiets)     │ (cross-type comparable)  │
│                    │                          │                          │
│ Computation depth  │ Same at all depths       │ Adaptive per depth       │
│ scaling            │                          │ (full at root, min deep) │
│                    │                          │                          │
│ Threat awareness   │ Implicit (through search)│ Explicit (threat matcher,│
│                    │                          │ threat-response killers) │
│                    │                          │                          │
│ Context sensitivity│ Minimal (pawn history)   │ Phase, structure, king   │
│                    │                          │ zone, threats, plan      │
│                    │                          │                          │
│ Move semantics     │ None                     │ Developing, attacking,   │
│                    │                          │ defending, prophylactic  │
└────────────────────┴─────────────────────────┴──────────────────────────┘
```

---

## IX. Lộ Trình Triển Khai

```
Phase 1 (Tháng 1-3): Foundation
├── Implement Context History System (thay thế butterfly history)
├── Implement Enhanced Killer System  
├── Implement Enhanced SEE+
├── Testing framework: measure ordering quality metrics
└── Target: +15-25 Elo vs baseline

Phase 2 (Tháng 4-6): Tactical Layer
├── Implement Tactical Motif Detector
├── Implement Threat Response Matcher
├── Implement Hanging Piece Resolver
├── Implement Lazy Move Ordering (phased generation)
└── Target: +25-40 Elo vs baseline

Phase 3 (Tháng 7-9): Strategic Layer  
├── Implement Phase-Aware Ordering
├── Implement Structure-Aware Ordering
├── Implement Plan Continuity Tracker
├── Implement Adaptive Score Fusion
└── Target: +35-55 Elo vs baseline

Phase 4 (Tháng 10-12): Predictive Layer
├── Train Move Quality Predictor
├── Train Position-Move Compatibility Network
├── Implement Online Learning System
├── Full integration testing
├── Performance optimization (SIMD, incremental)
└── Target: +40-80 Elo vs baseline

Phase 5 (Tháng 13-15): Optimization & Deployment
├── Quantization of neural components
├── SIMD batch scoring
├── Incremental feature computation
├── Memory optimization
├── Cross-platform testing
└── Target: Maintain Elo with acceptable NPS overhead
```

Kiến trúc HAMO biến move ordering từ một **bộ heuristics đơn giản** thành một **hệ thống quyết định đa tầng**, có khả năng hiểu context, dự đoán, và tự cải thiện — đồng thời giữ hiệu năng khả thi thông qua thiết kế phân tầng computation budget.