# Các Thuật Toán Stockfish Có Thể Cải Thiện \& Đề Xuất Nâng Cấp

## I. Tổng Quan Kiến Trúc Stockfish Hiện Tại

```
┌─────────────────────────────────────────────────────────┐
│                   STOCKFISH ENGINE                       │
│                                                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐│
│  │  Search   │  │  Move    │  │Evaluation│  │  Time   ││
│  │  Core     │  │  Ordering│  │  (NNUE)  │  │  Mgmt   ││
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬────┘│
│       │              │             │              │      │
│  ┌────┴─────┐  ┌────┴─────┐  ┌────┴─────┐  ┌────┴────┐│
│  │Alpha-Beta│  │  Hash    │  │  Pruning  │  │  Thread ││
│  │Negamax   │  │  Move    │  │  \& Reduct.│  │  Mgmt   ││
│  │Framework │  │  Killer  │  │  Heuristic│  │  (LazySMP)│
│  │          │  │  History │  │           │  │         ││
│  └──────────┘  └──────────┘  └──────────┘  └─────────┘│
└─────────────────────────────────────────────────────────┘
```

---

## II. Phân Tích Từng Thuật Toán \& Đề Xuất Cải Thiện

---

## 1\. NNUE Evaluation

### Hiện trạng

```
Kiến trúc hiện tại: HalfKA(P)\_1024x2 → 16 → 32 → 1

Input:  King-piece features (HalfKA)
        ~49,000 active features per side

Layer 1: 1024 neurons (perspective-based, incrementally updated)  
Layer 2: 16 neurons (merged both perspectives)
Layer 3: 32 neurons
Output:  1 neuron (centipawn evaluation)

Activation: ClippedReLU (CReLU)
Update: Efficiently incremental on make/unmake move
```

### Hạn chế hiện tại

```
1. Feature Representation hạn chế
   - HalfKA chỉ encode quan hệ King-Piece
   - Thiếu quan hệ Piece-Piece trực tiếp
   - Thiếu thông tin cấu trúc tốt (pawn structure)
   - Thiếu pattern phức tạp (battery, pin, skewer trừu tượng)

2. Network quá nông
   - Chỉ 3 hidden layers → hạn chế abstraction capacity
   - Không có skip connections hay attention mechanism
   - Không thể học long-range dependencies trên bàn cờ

3. Thiếu Context-Awareness
   - Không phân biệt game phase dynamically
   - Cùng weight cho mọi loại thế cờ
   - Không adapt theo opponent's likely plan

4. Training Pipeline cứng nhắc
   - Training từ self-play ở fixed depth
   - Không curriculum learning
   - Không adversarial training có hệ thống
```

### Đề xuất cải thiện

```
┌──────────────────────────────────────────────────────────┐
│           NNUE v2: Context-Aware Deep NNUE                │
│                                                           │
│  ┌─────────────────────────────────────────────────────┐ │
│  │  Multi-Feature Input Layer                          │ │
│  │                                                     │ │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────┐│ │
│  │  │ HalfKA   │ │PawnStruct│ │PiecePair │ │Mobility││ │
│  │  │(existing)│ │Features  │ │Features  │ │Features││ │
│  │  └────┬─────┘ └────┬─────┘ └────┬─────┘ └───┬────┘│ │
│  │       └──────┬──────┴──────┬─────┴──────┬────┘     │ │
│  └──────────────┼─────────────┼────────────┼──────────┘ │
│                 ▼             ▼            ▼             │
│  ┌──────────────────────────────────────────────────┐   │
│  │  Feature Fusion Layer (2048 neurons)              │   │
│  │  + Batch Normalization                            │   │
│  └────────────────────┬─────────────────────────────┘   │
│                       ▼                                  │
│  ┌──────────────────────────────────────────────────┐   │
│  │  Phase-Gated Hidden Layer                         │   │
│  │                                                   │   │
│  │  gate = σ(W\_phase · phase\_features + b)           │   │
│  │  output = gate ⊙ midgame\_path                     │   │
│  │         + (1-gate) ⊙ endgame\_path                 │   │
│  │                                                   │   │
│  │  Midgame path: 512 → 256 neurons                 │   │
│  │  Endgame path: 512 → 256 neurons                 │   │
│  └────────────────────┬─────────────────────────────┘   │
│                       ▼                                  │
│  ┌──────────────────────────────────────────────────┐   │
│  │  Residual Block (256 → 256, with skip connection) │   │
│  └────────────────────┬─────────────────────────────┘   │
│                       ▼                                  │
│  ┌──────────────────────────────────────────────────┐   │
│  │  Output: eval (centipawns) + confidence (0-1)     │   │
│  └──────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────┘
```

#### Các feature mới cụ thể

```python
class PawnStructureFeatures:
    """96 features mô tả cấu trúc tốt"""
    
    features = \[
        # Per-file pawn presence (16 features: 8 files × 2 sides)
        "pawn\_on\_file\_a\_white", ..., "pawn\_on\_file\_h\_black",
        
        # Pawn islands (4 features: count per side, sizes)
        "white\_pawn\_islands", "black\_pawn\_islands",
        "white\_island\_sizes", "black\_island\_sizes",
        
        # Structural defects (24 features)
        "doubled\_pawns\_per\_file",      # 8 features
        "isolated\_pawns\_per\_file",     # 8 features  
        "backward\_pawns\_per\_file",     # 8 features
        
        # Passed pawns (32 features)
        "passed\_pawn\_rank\_file",       # 16 features per side
        "passed\_pawn\_support\_type",    # connected, protected, outside
        
        # Pawn chains (20 features)
        "chain\_base\_square",
        "chain\_length",
        "chain\_direction",             # kingside/queenside/center
        "ram\_positions",               # locked pawn pairs
    ]
    
    def compute\_incrementally(self, move):
        """Chỉ cập nhật khi tốt di chuyển hoặc bị bắt"""
        if move.piece\_type == PAWN or move.is\_capture\_of\_pawn:
            self.update\_affected\_features(move)
        # Nếu không liên quan đến tốt → giữ nguyên (O(1))


class PiecePairFeatures:
    """128 features mô tả quan hệ giữa các cặp quân"""
    
    features = \[
        # Bishop pair (2)
        "white\_has\_bishop\_pair", "black\_has\_bishop\_pair",
        
        # Piece coordination (48)
        # Rook pairs on same rank/file/open file
        "rooks\_connected", "rooks\_on\_7th",
        "rook\_behind\_passed\_pawn",
        
        # Bishop-Knight imbalance context (16)
        "bishop\_in\_open\_position",     # bishop better
        "knight\_with\_outpost",         # knight better
        "pawns\_on\_bishop\_color",       # bad bishop detection
        
        # Queen-minor piece battery (16)
        "queen\_bishop\_battery\_diagonal",
        "queen\_rook\_battery\_file",
        
        # Piece vs pawn imbalance (24)
        "exchange\_up\_with\_pawns",
        "minor\_piece\_vs\_pawns",
        
        # Defensive coordination (22)
        "pieces\_defending\_king\_zone",
        "overloaded\_defender",
    ]

class MobilityFeatures:
    """64 features mô tả mobility ngắn gọn"""
    
    features = \[
        # Per-piece-type average mobility (10)
        "avg\_knight\_mobility\_white", "avg\_knight\_mobility\_black",
        "avg\_bishop\_mobility\_white", "avg\_bishop\_mobility\_black",
        "avg\_rook\_mobility\_white", "avg\_rook\_mobility\_black",
        "queen\_mobility\_white", "queen\_mobility\_black",
        "king\_mobility\_white", "king\_mobility\_black",
        
        # Control of key squares (32)
        "center\_control\_balance",      # e4,d4,e5,d5
        "extended\_center\_control",     # c3-f3 to c6-f6
        "king\_zone\_control\_white", "king\_zone\_control\_black",
        
        # Space advantage (8)
        "space\_behind\_pawns\_white", "space\_behind\_pawns\_black",
        
        # Trapped/restricted pieces (14)
        "trapped\_pieces\_white", "trapped\_pieces\_black",
        "restricted\_mobility\_pieces",  # pieces with < 3 moves
    ]
```

#### Confidence Output

```
Ý tưởng mới: NNUE output thêm confidence score

Output = (eval, confidence)

confidence cao (>0.8): Thế cờ rõ ràng, có thể search nông hơn
confidence thấp (<0.3): Thế cờ phức tạp, cần search sâu hơn

Ứng dụng trong search:
- Depth adjustment dựa trên confidence
- Pruning aggressiveness scale theo confidence
- Time allocation influenced by confidence
```

---

## 2\. Move Ordering

### Hiện trạng

```
Stockfish move ordering priority:
1. TT Move (Hash move)           - từ transposition table
2. Capture moves (MVV-LVA + SEE) - Most Valuable Victim, Least Valuable Attacker
3. Killer moves                  - 2 killer moves per ply
4. Counter move                  - 1 counter move cho previous move
5. History heuristic             - butterfly history + continuation history
6. Quiet moves                   - còn lại, sắp xếp theo history score

Quiet move ordering chi tiết:
- Main history: \[side]\[from]\[to] table
- Continuation history: \[prev\_piece]\[prev\_to]\[curr\_piece]\[curr\_to]
- Capture history: \[piece]\[to]\[captured\_type]
- Pawn history: \[pawn\_structure\_index]\[piece]\[to]
```

### Hạn chế

```
1. History tables bị "ô nhiễm" (pollution)
   - Scores tích lũy qua nhiều search → stale data
   - Aging mechanism đơn giản (divide by 2 periodically)
   - Không phân biệt context (opening vs endgame)

2. Killer moves quá đơn giản
   - Chỉ lưu 2 killers per ply
   - Không xét similarity giữa positions
   - Killer từ sibling node có thể không relevant

3. Counter move heuristic hạn chế
   - Chỉ 1 counter move per (piece, to\_square)
   - Không capture piece type
   - Thiếu deeper refutation patterns

4. Thiếu pattern-based ordering
   - Không recognize "loại nước đi" (developing, attacking, defensive...)
   - Không xét plan continuity
```

### Đề xuất cải thiện

#### A. Contextual History Heuristic

```python
class ContextualHistory:
    """History heuristic nhận biết context"""
    
    def \_\_init\_\_(self):
        # Thay vì 1 bảng history, dùng nhiều bảng theo context
        
        # Phase-specific history
        self.opening\_history = HistoryTable()   # piece\_count > 24
        self.middlegame\_history = HistoryTable() # 10 < piece\_count <= 24
        self.endgame\_history = HistoryTable()    # piece\_count <= 10
        
        # Structure-specific history  
        self.open\_position\_history = HistoryTable()
        self.closed\_position\_history = HistoryTable()
        
        # King-safety-context history
        self.attacking\_history = HistoryTable()  # opponent king exposed
        self.defending\_history = HistoryTable()  # our king under attack
        self.neutral\_history = HistoryTable()    # balanced king safety
        
    def get\_score(self, position, move):
        phase = classify\_phase(position)
        structure = classify\_structure(position)
        king\_context = classify\_king\_safety(position)
        
        # Weighted combination of relevant histories
        score = (0.5 \* self.phase\_table(phase).get(move) +
                 0.3 \* self.structure\_table(structure).get(move) +
                 0.2 \* self.king\_table(king\_context).get(move))
        
        return score
    
    def update(self, position, move, depth, is\_best):
        """Cập nhật tất cả bảng liên quan"""
        phase = classify\_phase(position)
        structure = classify\_structure(position)
        king\_context = classify\_king\_safety(position)
        
        bonus = depth \* depth  # standard depth-squared bonus
        
        self.phase\_table(phase).update(move, bonus, is\_best)
        self.structure\_table(structure).update(move, bonus, is\_best)
        self.king\_table(king\_context).update(move, bonus, is\_best)
```

#### B. Extended Killer \& Refutation System

```python
class ExtendedKillerSystem:
    """Mở rộng killer moves với pattern matching"""
    
    def \_\_init\_\_(self):
        self.killers = {}          # ply → \[killer1, killer2, killer3, killer4]
        self.threat\_killers = {}   # threat\_type → best\_response
        self.piece\_killers = {}    # piece\_type → recent successful moves
        self.zone\_killers = {}     # board\_zone → recent successful moves
        
    def get\_killers(self, ply, position):
        result = \[]
        
        # Standard killers
        if ply in self.killers:
            result.extend(self.killers\[ply])
        
        # Threat-response killers
        # Nhận diện threat tương tự ở ply trước
        current\_threats = identify\_threats(position)
        for threat in current\_threats:
            threat\_type = classify\_threat(threat)
            if threat\_type in self.threat\_killers:
                result.append(self.threat\_killers\[threat\_type])
        
        # Piece-type killers
        # "Mã thường nhảy đến ô tốt trong thế cờ tương tự"
        for piece\_type in active\_piece\_types(position):
            if piece\_type in self.piece\_killers:
                for candidate in self.piece\_killers\[piece\_type]:
                    if is\_legal(position, candidate):
                        result.append(candidate)
        
        # Zone killers
        # "Vùng cánh vua thường có nước hay ở thế tương tự"
        active\_zone = identify\_active\_zone(position)
        if active\_zone in self.zone\_killers:
            result.extend(self.zone\_killers\[active\_zone])
        
        return deduplicate(result)
```

#### C. Neural Move Ordering (NMO)

```
Ý tưởng: Dùng tiny neural network chuyên cho move ordering

┌─────────────────────────────────────┐
│     Neural Move Ordering (NMO)       │
│                                      │
│  Input: Position features (compact)  │
│         Move features                │
│                                      │
│  ┌─────────────────────────────────┐│
│  │ Position: 128 features          ││
│  │ - Material (12)                 ││
│  │ - King safety (16)              ││
│  │ - Pawn structure (32)           ││
│  │ - Piece placement (48)          ││
│  │ - Phase \& tempo (20)            ││
│  └──────────────┬──────────────────┘│
│                 │                    │
│  ┌──────────────┴──────────────────┐│
│  │ Move: 32 features               ││
│  │ - Piece type (6)                ││
│  │ - From/To squares (14)          ││
│  │ - Capture info (6)              ││
│  │ - Gives check (1)              ││
│  │ - History score (normalized)(5) ││
│  └──────────────┬──────────────────┘│
│                 ▼                    │
│  ┌─────────────────────────────────┐│
│  │ Hidden: 64 → 32 neurons         ││
│  │ Activation: ReLU                 ││
│  └──────────────┬──────────────────┘│
│                 ▼                    │
│  │ Output: move\_priority (scalar)   │
│                                      │
│  Cost: ~5μs per move evaluation      │
│  Chỉ dùng ở depth >= 6 (near root)  │
└─────────────────────────────────────┘

Training: Từ PV moves trong self-play games
Label: 1 nếu move là best move, 0 otherwise
Loss: Binary cross-entropy + ranking loss
```

---

## 3\. Pruning \& Reduction Techniques

### Hiện trạng Stockfish

```
Stockfish sử dụng rất nhiều kỹ thuật pruning/reduction:

A. Null Move Pruning (NMP)
   - Cho đối phương đi 2 nước liên tiếp (skip our turn)
   - Nếu vẫn tốt → prune (vị trí quá tốt, không cần search thêm)
   - Reduction: R = 4 + depth/6 + (eval - beta) / 200

B. Late Move Reduction (LMR)
   - Nước đi sau cùng trong move ordering ít khả năng tốt
   - Giảm depth cho nước thứ 4+ trong danh sách
   - Reduction phụ thuộc depth, move\_count, history score

C. Futility Pruning
   - Nếu eval + margin < alpha → prune (quá tệ, không thể cải thiện)
   - Margin = f(depth): depth 1 → 200cp, depth 2 → 400cp...

D. Reverse Futility Pruning (Static Null Move Pruning)
   - Nếu eval - margin >= beta → cutoff
   - "Đã quá tốt, không cần search"

E. Razoring
   - Ở depth nông, nếu eval + margin < alpha
   - Drop vào quiescence search trực tiếp

F. SEE Pruning
   - Prune nước bắt quân mà SEE < 0
   - Prune quiet moves mà SEE < threshold

G. Singular Extensions
   - Nếu TT move rõ ràng tốt nhất (singular)
   - Extend search depth cho move đó +1

H. Multi-Cut
   - Ở cut-nodes, thử vài nước đầu
   - Nếu >= 3 nước gây cutoff ở reduced depth → prune

I. ProbCut
   - Search nông với beta window rộng hơn
   - Nếu result > threshold → prune subtree
```

### Hạn chế chung

```
1. Margin/threshold hầu hết là hand-tuned constants
   → Không adapt theo position type
   → Có thể quá aggressive hoặc quá conservative

2. Pruning decisions không phối hợp
   → Mỗi technique hoạt động độc lập
   → Có thể prune cùng subtree nhiều lần (redundant) 
   → Hoặc bỏ sót subtree quan trọng

3. "One-size-fits-all" approach
   → Cùng reduction formula cho tactical và positional positions
   → Không tăng depth cho thế cờ cần tính toán sâu

4. Thiếu learning-based adaptation
   → Parameters không tự cải thiện qua experience
   → Dựa vào manual tuning (SPSA optimization)
```

### Đề xuất cải thiện

#### A. Adaptive Pruning Controller (APC)

```python
class AdaptivePruningController:
    """Điều khiển pruning tự thích ứng theo thế cờ"""
    
    def \_\_init\_\_(self):
        # Trained parameters cho mỗi loại thế cờ
        self.tactical\_profile = PruningProfile(
            nmp\_reduction\_scale=0.7,     # Bớt aggressive
            lmr\_reduction\_scale=0.6,     # Bớt aggressive  
            futility\_margin\_scale=0.5,   # Margin nhỏ hơn
            see\_threshold\_scale=0.8,     # Ít prune captures
            singular\_extension\_scale=1.5  # Extend nhiều hơn
        )
        
        self.positional\_profile = PruningProfile(
            nmp\_reduction\_scale=1.2,     # Aggressive hơn
            lmr\_reduction\_scale=1.3,     # Aggressive hơn
            futility\_margin\_scale=1.5,   # Margin lớn hơn
            see\_threshold\_scale=1.2,     # Prune captures nhiều hơn
            singular\_extension\_scale=0.8  # Extend ít hơn
        )
        
        self.endgame\_profile = PruningProfile(
            nmp\_reduction\_scale=0.5,     # Cẩn thận (zugzwang risk)
            lmr\_reduction\_scale=0.9,
            futility\_margin\_scale=0.7,
            see\_threshold\_scale=1.0,
            singular\_extension\_scale=1.2
        )
    
    def classify\_position(self, position):
        """Phân loại thế cờ → chọn profile"""
        
        tactical\_score = compute\_tactical\_intensity(position)
        # Factors: hanging pieces, checks available, 
        # pins, forks, discovered attacks, king exposure
        
        if tactical\_score > 0.7:
            return self.tactical\_profile
        elif position.piece\_count <= 10:
            return self.endgame\_profile
        else:
            # Blend giữa tactical và positional
            alpha = tactical\_score
            return blend\_profiles(
                self.tactical\_profile, 
                self.positional\_profile, 
                alpha
            )
    
    def get\_lmr\_reduction(self, position, depth, move\_count, 
                           history\_score):
        """LMR reduction tự thích ứng"""
        profile = self.classify\_position(position)
        
        base\_reduction = LMR\_TABLE\[depth]\[move\_count]  # existing table
        
        # Scale theo profile
        reduction = base\_reduction \* profile.lmr\_reduction\_scale
        
        # Fine-tune theo history
        reduction -= history\_score / 8192
        
        # Fine-tune theo move characteristics
        if move.gives\_check:
            reduction -= 1
        if move.is\_passed\_pawn\_push:
            reduction -= 0.5
        if move.is\_castling:
            reduction -= 1
            
        # NNUE confidence adjustment
        if nnue\_confidence < 0.3:
            reduction \*= 0.7  # Thế phức tạp → ít reduce
        
        return max(0, int(reduction))
    
    def get\_null\_move\_reduction(self, position, depth, eval\_score, beta):
        profile = self.classify\_position(position)
        
        base\_R = 4 + depth // 6 + min((eval\_score - beta) // 200, 3)
        
        # Scale theo profile
        R = int(base\_R \* profile.nmp\_reduction\_scale)
        
        # Zugzwang detection
        if has\_zugzwang\_risk(position):
            R = max(R - 2, 1)  # Giảm reduction mạnh
        
        return R


def compute\_tactical\_intensity(position):
    """Tính mức độ tactical của thế cờ (0.0 - 1.0)"""
    
    score = 0.0
    
    # Hanging pieces
    hanging = count\_hanging\_pieces(position)
    score += hanging \* 0.15
    
    # Available checks
    checks = count\_checking\_moves(position)
    score += min(checks \* 0.05, 0.2)
    
    # Pins and skewers
    pins = count\_pins(position)
    score += pins \* 0.1
    
    # King exposure
    king\_safety = evaluate\_king\_safety(position)
    if king\_safety < -100:  # centipawns
        score += 0.2
    
    # Pawn tension (captures available)
    pawn\_tension = count\_pawn\_captures(position)
    score += pawn\_tension \* 0.03
    
    # Material imbalance (more likely tactical)
    if has\_material\_imbalance(position):
        score += 0.1
    
    return min(score, 1.0)
```

#### B. Coordinated Pruning System (CPS)

```python
class CoordinatedPruningSystem:
    """Phối hợp các kỹ thuật pruning để tránh redundancy"""
    
    def should\_prune(self, position, move, depth, alpha, beta, 
                     eval\_score, move\_count):
        """Unified pruning decision"""
        
        # Tính tổng pruning evidence từ nhiều nguồn
        evidence = PruningEvidence()
        
        # 1. Futility evidence
        if depth <= 3:
            futility\_margin = self.adaptive\_futility\_margin(depth, position)
            if eval\_score + futility\_margin <= alpha:
                evidence.add("futility", 
                           confidence=(alpha - eval\_score - futility\_margin) 
                           / futility\_margin)
        
        # 2. SEE evidence (cho captures)
        if move.is\_capture:
            see\_value = see(position, move)
            if see\_value < 0:
                evidence.add("see\_negative", 
                           confidence=abs(see\_value) / 300)
        
        # 3. History evidence (cho quiet moves)
        if not move.is\_capture:
            hist\_score = self.history.get\_score(position, move)
            if hist\_score < self.history\_threshold:
                evidence.add("bad\_history",
                           confidence=(self.history\_threshold - hist\_score)
                           / self.history\_threshold)
        
        # 4. Move count evidence
        if move\_count > 6:
            evidence.add("late\_move",
                        confidence=min((move\_count - 6) / 20, 1.0))
        
        # Decision: prune nếu tổng evidence đủ mạnh
        total\_confidence = evidence.aggregate()
        
        # Threshold phụ thuộc position type
        threshold = self.get\_prune\_threshold(position)
        
        return total\_confidence > threshold
    
    def get\_reduction(self, position, move, depth, evidence):
        """Unified reduction decision"""
        
        # Base LMR reduction
        base = LMR\_TABLE\[depth]\[move.count]
        
        # Adjust theo aggregated evidence
        # Thay vì nhiều adjustment riêng lẻ cộng dồn không kiểm soát
        adjustment = evidence.total\_confidence \* 2.0  # max +2 reduction
        
        # Counter-evidence (reasons NOT to reduce)
        if move.gives\_check:
            adjustment -= 1.0
        if move.is\_singular\_candidate:
            adjustment -= 1.5
        if evidence.has("tactical\_position"):
            adjustment -= 0.5 \* evidence.get("tactical\_position").confidence
        
        return max(0, int(base + adjustment))
```

#### C. Learning-Based Pruning Parameters

```python
class LearnedPruningParams:
    """Tự học pruning parameters qua self-play"""
    
    def \_\_init\_\_(self):
        # Tiny neural network cho pruning decisions
        self.pruning\_net = TinyNet(
            input\_size=32,   # position + move features
            hidden\_size=16,
            output\_size=3    # (should\_prune, reduction, extension)
        )
        
    def collect\_training\_data(self, search\_results):
        """Thu thập data từ search"""
        for node in search\_results:
            # Label: nước đi có ảnh hưởng đến kết quả không?
            was\_important = (node.subtree\_best\_score > node.parent\_alpha)
            
            features = extract\_pruning\_features(node)
            
            if was\_important and node.was\_pruned:
                # Sai: đã prune nước quan trọng
                self.data.add(features, label="should\_not\_prune")
            elif not was\_important and not node.was\_pruned:
                # Có thể prune nhưng đã không prune
                self.data.add(features, label="should\_prune")
    
    def train\_periodically(self):
        """Retrain mỗi N games"""
        self.pruning\_net.train(self.data)
        self.data.clear()
```

---

## 4\. Transposition Table (TT)

### Hiện trạng

```
Stockfish TT:
- Zobrist hashing (64-bit keys)
- Bucket system: mỗi bucket 3 entries
- Replacement strategy: depth-preferred + age-based
- Entry: hash\_key, move, score, depth, bound\_type, age

Kích thước mặc định: 16-256 MB (configurable)
```

### Hạn chế

```
1. Hash collisions
   - 64-bit key → xác suất collision ~10^-19 per probe
   - Nhưng với billions of probes → collision xảy ra
   - Type-1 (khác position, cùng key) → sai kết quả nghiêm trọng
   - Type-2 (cùng bucket, bị overwrite) → mất thông tin

2. Replacement strategy đơn giản
   - Chỉ xét depth và age
   - Không xét "giá trị thông tin" của entry
   - Entry ở position quan trọng bị overwrite bởi entry không quan trọng

3. Không chia sẻ thông tin giữa similar positions
   - Positions chỉ khác en passant hay castling rights → hash khác hoàn toàn
   - Không exploit structural similarity

4. Prefetching không tối ưu
   - Hardware prefetch chỉ cho bucket tiếp theo
   - Không dự đoán future positions cần probe
```

### Đề xuất cải thiện

#### A. Adaptive Replacement Strategy

```python
class AdaptiveTT:
    """Transposition table với replacement strategy thông minh"""
    
    class Entry:
        def \_\_init\_\_(self):
            self.key = 0
            self.move = None
            self.score = 0
            self.depth = 0
            self.bound = NONE
            self.age = 0
            self.access\_count = 0     # Mới: đếm số lần truy cập
            self.pv\_node = False      # Mới: có phải PV node?
            self.importance = 0.0     # Mới: importance score
    
    def compute\_importance(self, entry):
        """Tính mức quan trọng của entry"""
        importance = 0.0
        
        # Depth contribution (deeper = more valuable)
        importance += entry.depth \* 2.0
        
        # Access frequency (frequently accessed = important)
        importance += min(entry.access\_count, 10) \* 1.5
        
        # PV node bonus (PV entries cực kỳ quan trọng)
        if entry.pv\_node:
            importance += 20.0
        
        # Age penalty (older = less relevant)
        age\_diff = self.current\_age - entry.age
        importance -= age\_diff \* 3.0
        
        # Exact bound bonus (exact scores > bounds)
        if entry.bound == EXACT:
            importance += 5.0
        
        return importance
    
    def should\_replace(self, existing, new\_entry):
        """Quyết định có nên thay thế entry cũ không"""
        old\_importance = self.compute\_importance(existing)
        new\_importance = self.compute\_importance(new\_entry)
        
        # Luôn thay thế nếu cùng position
        if existing.key == new\_entry.key:
            return new\_entry.depth >= existing.depth - 3
        
        # So sánh importance
        return new\_importance > old\_importance \* 0.8  # hysteresis
```

#### B. Hierarchical TT

```
┌───────────────────────────────────────────────┐
│          Hierarchical Transposition Table       │
│                                                 │
│  ┌──────────────────────────────────────────┐  │
│  │  L1: Hot Cache (1 MB)                     │  │
│  │  - PV nodes only                          │  │
│  │  - No replacement (LRU only)              │  │
│  │  - Access time: ~1 ns                     │  │
│  └──────────────────┬───────────────────────┘  │
│                     │ miss                      │
│                     ▼                           │
│  ┌──────────────────────────────────────────┐  │
│  │  L2: Warm Cache (32 MB)                   │  │
│  │  - Recent high-depth entries              │  │
│  │  - Adaptive replacement                   │  │
│  │  - Access time: ~5 ns                     │  │
│  └──────────────────┬───────────────────────┘  │
│                     │ miss                      │
│                     ▼                           │
│  ┌──────────────────────────────────────────┐  │
│  │  L3: Cold Storage (256+ MB)               │  │
│  │  - All entries                            │  │
│  │  - Standard replacement                   │  │
│  │  - Access time: ~20 ns                    │  │
│  └──────────────────────────────────────────┘  │
│                                                 │
│  Promotion policy:                              │
│  - L3 → L2: on second access                   │
│  - L2 → L1: if PV node or access\_count > 5     │
│  Demotion: automatic via LRU                    │
└───────────────────────────────────────────────┘
```

#### C. Predictive Prefetching

```python
class PredictivePrefetch:
    """Prefetch TT entries cho positions sắp search"""
    
    def prefetch\_likely\_children(self, position, move\_list):
        """Prefetch TT entries cho các nước đi khả năng cao"""
        
        # Top 4 nước đi có khả năng search cao nhất
        top\_moves = move\_list\[:4]  # Đã sorted theo move ordering
        
        for move in top\_moves:
            # Tính Zobrist hash cho position sau move
            child\_hash = position.hash ^ zobrist\_delta(move)
            
            # Hardware prefetch vào L1 cache
            prefetch\_to\_l1(self.tt.address\_of(child\_hash))
        
        # Prefetch sâu hơn: grandchildren của top 1-2 moves
        for move in top\_moves\[:2]:
            child\_pos = position.make\_move(move)
            likely\_response = self.tt.probe(child\_pos.hash)
            
            if likely\_response and likely\_response.move:
                grandchild\_hash = (child\_pos.hash ^ 
                                   zobrist\_delta(likely\_response.move))
                prefetch\_to\_l2(self.tt.address\_of(grandchild\_hash))
            
            position.unmake\_move(move)
```

---

## 5\. Quiescence Search

### Hiện trạng

```
Stockfish quiescence search:
- Chỉ search captures, promotions, check evasions
- SEE pruning: skip captures mà SEE < 0
- Delta pruning: skip nếu material gain + margin < alpha
- Stand pat: dùng static eval làm lower bound
- Depth limit: thường 6-8 ply thêm
```

### Hạn chế

```
1. Horizon Effect
   - Quiet moves quan trọng bị bỏ qua (quiet sacrifice, zwischenzug)
   - Chỉ captures → miss "quiet tactics"
   
2. Quiescence Search Explosion
   - Trong thế phức tạp, QS có thể chiếm 60-80% nodes
   - Rất nhiều nodes là vô ích (không ảnh hưởng kết quả)

3. Stand Pat quá đơn giản
   - Static eval có thể sai lớn trong tactical positions
   - Đặc biệt khi có nhiều threats không phải captures

4. Không xét non-capture threats
   - Fork threats, pin threats, discovered attack threats
   - Chỉ resolved khi chúng materialize thành captures
```

### Đề xuất cải thiện

#### A. Threat-Aware Quiescence (TAQ-Search)

```python
def quiescence\_v2(position, alpha, beta, depth\_left=8):
    """Quiescence search mở rộng với threat awareness"""
    
    # Stand pat with threat adjustment
    stand\_pat = nnue\_eval(position)
    
    # NEW: Điều chỉnh stand pat theo unresolved threats
    threats = detect\_unresolved\_threats(position)
    threat\_adjustment = sum(t.expected\_loss for t in threats)
    adjusted\_stand\_pat = stand\_pat - threat\_adjustment \* 0.3
    
    if adjusted\_stand\_pat >= beta:
        return beta
    if adjusted\_stand\_pat > alpha:
        alpha = adjusted\_stand\_pat
    
    if depth\_left == 0:
        return alpha
    
    # Generate moves: captures + critical quiet moves
    moves = generate\_captures(position)
    
    # NEW: Thêm "critical quiet moves" - không phải captures nhưng rất quan trọng
    if depth\_left >= 4:  # Chỉ ở depth đủ sâu
        critical\_quiets = detect\_critical\_quiet\_moves(position)
        moves.extend(critical\_quiets)
    
    # Sort by expected gain
    moves.sort(key=lambda m: expected\_gain(position, m), reverse=True)
    
    for move in moves:
        # Enhanced SEE pruning
        if move.is\_capture:
            see\_value = see(position, move)
            if see\_value < -50 \* (depth\_left):  # Scale với depth
                continue
        
        # NEW: Critical quiet move pruning
        if move.is\_critical\_quiet:
            if not verify\_criticality(position, move):
                continue
        
        position.make\_move(move)
        score = -quiescence\_v2(position, -beta, -alpha, depth\_left - 1)
        position.unmake\_move(move)
        
        if score >= beta:
            return beta
        if score > alpha:
            alpha = score
    
    return alpha


def detect\_critical\_quiet\_moves(position):
    """Phát hiện nước quiet nhưng critical"""
    critical = \[]
    
    # 1. Fork threats (Mã/Tượng/Hậu đe dọa fork)
    for piece in position.pieces(side\_to\_move):
        for target\_sq in piece.legal\_moves():
            if creates\_fork(position, piece, target\_sq):
                critical.append(Move(piece, target\_sq, "fork\_threat"))
    
    # 2. Discovered attack setup
    for piece in position.pieces(side\_to\_move):
        for target\_sq in piece.legal\_moves():
            if creates\_discovered\_attack(position, piece, target\_sq):
                critical.append(Move(piece, target\_sq, "discovery"))
    
    # 3. Zwischenzug candidates
    if position.is\_in\_exchange\_sequence():
        for move in generate\_quiet\_moves(position):
            if is\_zwischenzug(position, move):
                critical.append(move)
    
    # 4. Promotion threats (pawn push tạo threat promote)
    for pawn in position.pawns(side\_to\_move):
        if pawn.rank >= 6:  # 7th rank push
            push = pawn\_push(pawn)
            if push.is\_legal:
                critical.append(push)
    
    # Giới hạn số lượng để không explode
    return sorted(critical, key=lambda m: m.priority)\[:5]
```

#### B. Lazy Quiescence

```python
def lazy\_quiescence(position, alpha, beta):
    """Quiescence nhanh cho nhánh ít quan trọng"""
    
    # Phase 1: Ultra-fast estimation
    material\_eval = material\_count(position)
    max\_possible\_gain = max\_capturable\_piece\_value(position)
    
    # Nếu ngay cả capture tốt nhất cũng không đủ → skip
    if material\_eval + max\_possible\_gain + 200 < alpha:
        return material\_eval  # Fast return
    
    # Nếu đã quá tốt → skip
    if material\_eval - max\_losable\_piece\_value(position) >= beta:
        return beta
    
    # Phase 2: Chỉ search top captures
    stand\_pat = nnue\_eval(position)
    if stand\_pat >= beta:
        return beta
    
    top\_captures = get\_top\_n\_captures(position, n=3)  # Chỉ top 3
    
    for capture in top\_captures:
        if see(position, capture) < 0:
            continue
        position.make\_move(capture)
        score = -lazy\_quiescence(position, -beta, -max(alpha, stand\_pat))
        position.unmake\_move(capture)
        
        if score >= beta:
            return beta
        if score > stand\_pat:
            stand\_pat = score
    
    return max(alpha, stand\_pat)
```

---

## 6\. Time Management

### Hiện trạng

```
Stockfish time management:
- Allocate base time = remaining\_time / moves\_to\_go
- Adjust: +time nếu eval unstable (fluctuating between iterations)
- Adjust: +time nếu best move changed recently
- Adjust: -time nếu best move stable many iterations
- Hard limit: không vượt quá max\_time
- Soft limit: target time, có thể extend
```

### Hạn chế

```
1. Không xét position complexity
   - Thế đơn giản (1 nước tốt rõ ràng) vẫn dùng nhiều thời gian
   - Thế phức tạp (nhiều nước tương đương) có thể thiếu thời gian

2. Không xét game context
   - Không biết đang ở critical phase
   - Không xét opponent's clock
   - Không xét score trajectory

3. Move stability metric quá đơn giản
   - Chỉ xét "best move có thay đổi không?"
   - Không xét score change magnitude
   - Không xét alternative moves quality
```

### Đề xuất cải thiện

```python
class AdvancedTimeManager:
    """Time management thông minh hơn"""
    
    def allocate\_time(self, position, game\_state):
        base\_time = self.compute\_base\_time(game\_state)
        
        # 1. Complexity adjustment
        complexity = self.evaluate\_complexity(position)
        complexity\_factor = 0.5 + complexity  # 0.5x - 1.5x
        
        # 2. Criticality adjustment
        criticality = self.evaluate\_criticality(position, game\_state)
        criticality\_factor = 0.7 + 0.6 \* criticality  # 0.7x - 1.3x
        
        # 3. Trend adjustment
        trend = self.evaluate\_score\_trend(game\_state)
        # Đang mất dần lợi thế → nghĩ lâu hơn
        # Đang thắng rõ → đi nhanh
        trend\_factor = 1.0 - 0.3 \* trend  # 0.7x - 1.3x
        
        # 4. Opponent clock pressure
        clock\_ratio = game\_state.my\_time / max(game\_state.opp\_time, 1)
        if clock\_ratio > 3.0:
            # Ta nhiều thời gian hơn nhiều → có thể nghĩ lâu
            clock\_factor = 1.2
        elif clock\_ratio < 0.3:
            # Ta ít thời gian hơn nhiều → đi nhanh
            clock\_factor = 0.6
        else:
            clock\_factor = 1.0
        
        # 5. Move difficulty prediction
        difficulty = self.predict\_move\_difficulty(position)
        difficulty\_factor = 0.6 + 0.8 \* difficulty
        
        allocated = (base\_time \* complexity\_factor \* criticality\_factor 
                    \* trend\_factor \* clock\_factor \* difficulty\_factor)
        
        return clamp(allocated, 
                    min\_time=base\_time \* 0.2,
                    max\_time=base\_time \* 3.0)
    
    def evaluate\_complexity(self, position):
        """0.0 = đơn giản, 1.0 = rất phức tạp"""
        factors = \[]
        
        # Material complexity
        piece\_count = position.piece\_count
        factors.append(min(piece\_count / 32, 1.0))
        
        # Tactical complexity
        factors.append(compute\_tactical\_intensity(position))
        
        # Move count (nhiều nước hợp lệ = phức tạp hơn)
        legal\_moves = len(position.legal\_moves())
        factors.append(min(legal\_moves / 50, 1.0))
        
        # Evaluation uncertainty
        if hasattr(position, 'nnue\_confidence'):
            factors.append(1.0 - position.nnue\_confidence)
        
        # Pawn structure complexity
        open\_files = count\_open\_files(position)
        factors.append(open\_files / 8)
        
        return sum(factors) / len(factors)
    
    def should\_stop\_early(self, search\_info):
        """Quyết định dừng sớm dựa trên search stability"""
        
        # Condition 1: Best move cực kỳ stable
        if search\_info.best\_move\_stable\_iterations >= 8:
            if search\_info.score\_variance < 5:  # centipawns
                return True
        
        # Condition 2: Nước đi rõ ràng vượt trội
        if search\_info.best\_score - search\_info.second\_best\_score > 200:
            return True
        
        # Condition 3: Forced move (chỉ 1 nước hợp lệ)
        if search\_info.legal\_move\_count == 1:
            return True
        
        # Condition 4: Score đã converge
        recent\_scores = search\_info.scores\[-5:]
        if len(recent\_scores) == 5:
            if max(recent\_scores) - min(recent\_scores) < 3:
                return True
        
        return False
    
    def should\_extend\_time(self, search\_info):
        """Quyết định xin thêm thời gian"""
        
        # Condition 1: Best move vừa thay đổi ở depth sâu
        if (search\_info.best\_move\_changed and 
            search\_info.current\_depth >= 15):
            return True
        
        # Condition 2: Score drop đáng kể
        if search\_info.score\_drop > 50:
            return True
        
        # Condition 3: Fail low ở root
        if search\_info.root\_fail\_low:
            return True
        
        # Condition 4: Nhiều nước gần bằng nhau
        if search\_info.moves\_within\_10cp >= 4:
            return True
        
        return False
```

---

## 7\. Lazy SMP (Parallel Search)

### Hiện trạng

```
Stockfish Lazy SMP:
- Multiple threads search cùng root position
- Mỗi thread có depth offset khác nhau (stagger depths)
- Share transposition table (lock-free)
- Không phân chia cây tìm kiếm explicitly
- Thread 0 là "main thread", quyết định kết quả cuối
```

### Hạn chế

```
1. Scaling kém ở nhiều threads
   - 2 threads ≈ +70 Elo
   - 4 threads ≈ +120 Elo
   - 8 threads ≈ +160 Elo
   - 64 threads ≈ +250 Elo
   - Diminishing returns rõ rệt

2. Duplicate work
   - Nhiều threads search cùng subtree
   - Waste computation, đặc biệt ở top of tree

3. TT contention
   - Nhiều threads write/read cùng bucket
   - False sharing trên cache lines
   - Lock-free nhưng vẫn có memory ordering overhead

4. No work stealing
   - Thread idle không thể nhận work từ thread bận
   - Thread xong sớm ngồi chờ hoặc re-search vô ích
```

### Đề xuất cải thiện

#### A. Hybrid Parallel Search

```python
class HybridParallelSearch:
    """Kết hợp Lazy SMP + ABDADA + Work Stealing"""
    
    def \_\_init\_\_(self, num\_threads):
        self.threads = \[SearchThread(i) for i in range(num\_threads)]
        self.work\_queue = ConcurrentDeque()
        self.shared\_tt = LockFreeTranspositionTable()
        
        # ABDADA: "Alpha-Beta with Deferred Deepening of All subtrees"
        self.node\_markers = AtomicBitset()  # Track which nodes being searched
    
    def search\_root(self, position, depth):
        # Phase 1: Main thread searches to depth D
        root\_moves = generate\_moves(position)
        
        # Phase 2: Distribute subtrees
        # Sau khi main thread xong depth D-2:
        # → Biết rough ordering
        # → Phân chia subtrees cho other threads
        
        # Thread 0: PV node (always deepest search)
        self.threads\[0].assign(root\_moves\[0], depth)
        
        # Threads 1-3: Next best moves (slightly less depth)
        for i, move in enumerate(root\_moves\[1:4]):
            self.threads\[i+1].assign(move, depth - 1)
        
        # Remaining threads: Lazy SMP style (different depths)
        for i in range(4, len(self.threads)):
            offset = (i % 3)  # depth offsets: 0, 1, 2
            self.threads\[i].assign\_lazy\_smp(position, depth + offset)
        
        # Phase 3: Work stealing
        self.enable\_work\_stealing()
        
        # Phase 4: Wait and collect results
        results = self.wait\_all()
        return self.aggregate\_results(results)
    
    def work\_stealing\_loop(self, idle\_thread):
        """Thread idle tìm work từ thread bận"""
        while not self.search\_finished:
            # Tìm thread có nhiều work nhất
            busiest = max(self.threads, 
                         key=lambda t: t.remaining\_nodes\_estimate)
            
            if busiest.remaining\_nodes\_estimate > 1000:
                # Steal một subtree
                stolen\_work = busiest.split\_work()
                if stolen\_work:
                    idle\_thread.execute(stolen\_work)
            else:
                # Không có work để steal → Lazy SMP fallback
                idle\_thread.search\_lazy\_smp()
```

#### B. NUMA-Aware TT

```python
class NUMATranspositionTable:
    """TT tối ưu cho NUMA architecture"""
    
    def \_\_init\_\_(self, total\_size, num\_numa\_nodes):
        # Mỗi NUMA node có local TT partition
        partition\_size = total\_size // num\_numa\_nodes
        
        self.local\_tt = {}
        for node in range(num\_numa\_nodes):
            # Allocate memory trên NUMA node tương ứng
            self.local\_tt\[node] = allocate\_on\_numa\_node(
                partition\_size, node
            )
        
        # Shared overflow area (smaller)
        self.overflow = allocate\_interleaved(total\_size // 4)
    
    def probe(self, hash\_key, numa\_node):
        """Probe ưu tiên local TT"""
        
        # Step 1: Probe local partition (fast, no remote memory access)
        local\_partition = hash\_key % self.num\_partitions
        if local\_partition == numa\_node:
            result = self.local\_tt\[numa\_node].probe(hash\_key)
            if result:
                return result
        
        # Step 2: Probe remote partition (slower)
        remote\_node = local\_partition
        result = self.local\_tt\[remote\_node].probe(hash\_key)
        if result:
            # Promote to local cache
            self.local\_tt\[numa\_node].cache(hash\_key, result)
            return result
        
        # Step 3: Probe overflow
        return self.overflow.probe(hash\_key)
```

---

## 8\. Search Extensions

### Hiện trạng

```
Stockfish extensions:
- Singular extension: +1 depth nếu TT move là singular (rõ ràng tốt nhất)
- Check extension: +1 depth khi cho check (đã giảm vai trò gần đây)
- Passed pawn extension: khi tốt thông tiến xa
- Capture extension: trong một số trường hợp recap
```

### Đề xuất cải thiện

```python
class SmartExtensionSystem:
    """Hệ thống extension thông minh hơn"""
    
    def compute\_extension(self, position, move, search\_context):
        extension = 0.0
        
        # 1. Singular Extension (giữ nguyên, đã tốt)
        if search\_context.is\_singular(move):
            extension += 1.0
        
        # 2. Tactical Complexity Extension (MỚI)
        # Extend khi phát hiện thế cờ tactical phức tạp
        tactical\_score = compute\_tactical\_intensity(position)
        if tactical\_score > 0.8 and search\_context.depth >= 8:
            extension += 0.5
        
        # 3. Critical Endgame Extension (MỚI)
        # Extend trong tàn cuộc quan trọng
        if is\_critical\_endgame(position):
            # KPK, KRK, etc. cần tính chính xác
            extension += 1.0
        
        # 4. Threat Extension (MỚI)
        # Extend khi có mối đe dọa nghiêm trọng không thể resolve bằng captures
        if has\_serious\_non\_capture\_threat(position, move):
            extension += 0.5
        
        # 5. Recapture Extension nâng cấp
        if move.is\_recapture:
            # Chỉ extend nếu recapture không rõ ràng
            if abs(see(position, move)) < 100:
                extension += 0.5
        
        # 6. Piece Sacrifice Extension (MỚI)
        # Extend khi phát hiện potential sacrifice
        if is\_potential\_sacrifice(position, move):
            extension += 1.0
        
        # 7. Pawn Race Extension (MỚI)
        # Trong tàn cuộc tốt, extend khi 2 bên đều có tốt thông
        if is\_pawn\_race(position):
            extension += 1.0
        
        # Budget: tổng extension không vượt quá giới hạn
        max\_ext = 2.0 if search\_context.is\_pv else 1.0
        return min(extension, max\_ext)
    
    def is\_potential\_sacrifice(self, position, move):
        """Phát hiện hy sinh tiềm năng"""
        if not move.is\_capture:
            return False
        
        see\_value = see(position, move)
        
        # Hy sinh: SEE negative nhưng có compensation
        if see\_value < -100:
            # Check compensation
            position.make\_move(move)
            
            compensation = 0
            
            # Attack on king
            if king\_attack\_score(position) > 300:
                compensation += 200
            
            # Passed pawn created
            if creates\_passed\_pawn(position, move):
                compensation += 100
            
            # Piece activity gain
            activity\_gain = piece\_activity\_delta(position)
            compensation += activity\_gain
            
            position.unmake\_move(move)
            
            return compensation > abs(see\_value) \* 0.5
        
        return False
```

---

## 9\. Opening Book \& Endgame Tablebases Integration

### Hiện trạng

```
Stockfish:
- Không có built-in opening book (dùng external polyglot/CTG)
- Syzygy tablebase support cho ≤ 7 pieces
- Tablebase probe ở search leaves
```

### Đề xuất cải thiện

```python
class SmartTablebaseIntegration:
    """Tích hợp tablebase thông minh hơn"""
    
    def probe\_with\_strategy(self, position, search\_context):
        """Probe tablebase có chiến lược"""
        
        piece\_count = position.piece\_count
        
        if piece\_count <= 7:
            # Standard probe
            tb\_result = syzygy\_probe(position)
            
            if tb\_result.is\_decided:
                # MỚI: Đánh giá "ease of win/defense"
                # Không chỉ biết thắng/thua/hòa, mà còn biết khó dễ
                
                if tb\_result.wdl == WIN:
                    # DTZ (Distance to Zeroing) cho biết bao nhiêu nước
                    dtz = tb\_result.dtz
                    
                    # Ease score: thắng nhanh → bonus cao
                    ease = 1.0 / (1 + dtz \* 0.01)
                    
                    # Score = large winning score - difficulty penalty
                    return 20000 - dtz + ease \* 100
                
                elif tb\_result.wdl == DRAW:
                    # Trong draw, vẫn cần đánh giá "quality of draw"
                    # Fortress draw vs barely holding draw
                    return evaluate\_draw\_quality(position)
        
        # MỚI: "Near-tablebase" positions (8-9 pieces)
        # Dùng learned evaluation cho positions gần tablebase
        if piece\_count <= 9:
            return near\_tablebase\_eval(position)
        
        return None

class NearTablebaseEvaluator:
    """Đánh giá thế cờ gần tablebase (8-9 quân)"""
    
    def \_\_init\_\_(self):
        # Neural network trained trên positions 
        # mà kết quả TB biết khi bớt 1-2 quân
        self.net = load\_near\_tb\_network()
    
    def evaluate(self, position):
        # Xét tất cả possible exchanges dẫn đến TB range
        reachable\_tb\_positions = \[]
        
        for exchange\_seq in enumerate\_exchanges(position):
            result\_pos = apply\_exchanges(position, exchange\_seq)
            if result\_pos.piece\_count <= 7:
                tb\_result = syzygy\_probe(result\_pos)
                reachable\_tb\_positions.append(
                    (exchange\_seq, tb\_result)
                )
        
        # Đánh giá dựa trên outcomes khả thi
        if all(r.wdl >= DRAW for \_, r in reachable\_tb\_positions):
            return "at least draw"
        
        # Neural evaluation cho complex cases
        return self.net.evaluate(position, reachable\_tb\_positions)
```

---

## 10\. Aspiration Windows

### Hiện trạng

```
Stockfish aspiration windows:
- Bắt đầu search mới với window \[prev\_score - delta, prev\_score + delta]
- delta ban đầu = 10 centipawns (rất hẹp)
- Nếu fail high/low → mở rộng window × 1.5 và re-search
- Dần mở rộng đến full window \[-INF, +INF] nếu cần
```

### Đề xuất cải thiện

```python
class AdaptiveAspirationWindow:
    """Aspiration window tự thích ứng"""
    
    def \_\_init\_\_(self):
        self.score\_history = \[]
        self.volatility\_estimate = 10  # Initial estimate
    
    def compute\_initial\_delta(self, position, prev\_score, prev\_depth):
        """Tính delta ban đầu thông minh"""
        
        # Dựa trên volatility lịch sử
        if len(self.score\_history) >= 3:
            recent = self.score\_history\[-3:]
            volatility = max(abs(recent\[i] - recent\[i-1]) 
                           for i in range(1, len(recent)))
            self.volatility\_estimate = (
                0.7 \* self.volatility\_estimate + 0.3 \* volatility
            )
        
        delta = max(10, int(self.volatility\_estimate \* 0.8))
        
        # Position complexity adjustment
        complexity = evaluate\_complexity(position)
        delta = int(delta \* (0.5 + complexity))  # 0.5x - 1.5x
        
        # Depth adjustment
        # Deeper search → more stable → narrower window
        depth\_factor = max(0.5, 1.0 - (prev\_depth - 10) \* 0.03)
        delta = int(delta \* depth\_factor)
        
        return clamp(delta, 5, 100)
    
    def compute\_asymmetric\_window(self, prev\_score, delta, position):
        """Window không đối xứng dựa trên context"""
        
        # Nếu đang thắng lớn → mở rộng phía dưới (catch mistakes)
        if prev\_score > 200:
            return (prev\_score - delta \* 1.5, prev\_score + delta \* 0.7)
        
        # Nếu đang thua lớn → mở rộng phía trên (find improvements)
        if prev\_score < -200:
            return (prev\_score - delta \* 0.7, prev\_score + delta \* 1.5)
        
        # Nếu thế cờ tactical → window rộng hơn
        if is\_tactical(position):
            return (prev\_score - delta \* 1.3, prev\_score + delta \* 1.3)
        
        return (prev\_score - delta, prev\_score + delta)
```

---

## III. Tổng Hợp Ước Tính Ảnh Hưởng

```
┌──────────────────────────────┬────────────┬──────────────────────┐
│         Cải thiện            │ Elo ước    │    Độ khó triển khai │
│                              │ tính       │                      │
├──────────────────────────────┼────────────┼──────────────────────┤
│ NNUE v2 (Context-Aware)     │ +30-60     │ ★★★★★ (Rất cao)     │
│ Contextual History           │ +10-20     │ ★★★ (Trung bình)    │
│ Neural Move Ordering         │ +5-15      │ ★★★★ (Cao)          │
│ Adaptive Pruning Controller  │ +15-30     │ ★★★★ (Cao)          │
│ Coordinated Pruning          │ +10-20     │ ★★★ (Trung bình)    │
│ Hierarchical TT              │ +5-10      │ ★★★ (Trung bình)    │
│ Predictive Prefetching       │ +3-8       │ ★★ (Thấp)           │
│ Threat-Aware Quiescence      │ +15-25     │ ★★★★ (Cao)          │
│ Advanced Time Management     │ +10-20     │ ★★ (Thấp)           │
│ Hybrid Parallel Search       │ +10-25     │ ★★★★★ (Rất cao)     │
│ Smart Extensions             │ +5-15      │ ★★★ (Trung bình)    │
│ Near-Tablebase Evaluation    │ +5-10      │ ★★★★ (Cao)          │
│ Adaptive Aspiration          │ +3-8       │ ★★ (Thấp)           │
├──────────────────────────────┼────────────┼──────────────────────┤
│ TỔNG (không cộng đơn giản)  │ +80-150    │                      │
│ Thực tế (overlap \& interact) │ +50-100    │                      │
└──────────────────────────────┴────────────┴──────────────────────┘

Ghi chú: Elo estimates dựa trên phân tích lý thuyết.
Thực tế phụ thuộc implementation quality và interaction effects.
Các cải thiện không cộng tuyến tính vì có overlap.
```

---

## IV. Lộ Trình Ưu Tiên

```
Priority 1 (Quick wins, low risk):
├── Advanced Time Management         (+10-20 Elo, dễ implement)
├── Adaptive Aspiration Windows       (+3-8 Elo, dễ implement)
└── Predictive TT Prefetching         (+3-8 Elo, dễ implement)

Priority 2 (Medium effort, good return):
├── Contextual History Heuristic      (+10-20 Elo)
├── Smart Extension System            (+5-15 Elo)
├── Hierarchical TT                   (+5-10 Elo)
└── Coordinated Pruning System        (+10-20 Elo)

Priority 3 (High effort, high return):
├── Adaptive Pruning Controller       (+15-30 Elo)
├── Threat-Aware Quiescence           (+15-25 Elo)
└── Near-Tablebase Evaluation         (+5-10 Elo)

Priority 4 (Research-level, highest potential):
├── NNUE v2 (Context-Aware)           (+30-60 Elo)
├── Neural Move Ordering              (+5-15 Elo)
└── Hybrid Parallel Search            (+10-25 Elo)
```

Mỗi cải thiện đều có thể triển khai **độc lập** và **đo lường** qua SPRT testing framework mà Stockfish đã có sẵn, cho phép validate từng thay đổi trước khi merge.

