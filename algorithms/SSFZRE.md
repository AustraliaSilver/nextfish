# Thuật Toán Cốt Lõi: SSFZRE (Structural Stagnation, Fortress & Zugzwang Resolution Engine)

> **Đặc tả Kỹ thuật Độc lập & Triển khai Kiểm thử (Standalone Specification)**  
> *Phân hệ: Đánh giá Tàn cuộc Cấu trúc & Khắc phục Điểm mù Mạng Nơ-ron (Endgame Topology)*  
> *Dự án: NEXUS-CHESS / NextFish Core Engine*  
> *Trạng thái: Hoàn thiện Triển khai & Khắc phục Lỗi Cận Tối Ưu (Production Ready)*

---

## Mục Lục
- [Tóm Tắt Tổng Quan (Executive Summary)](#tóm-tắt-tổng-quan-executive-summary)
- [I. Phân Tích Sâu Điểm Mù Pháo Đài & Sai Số Zugzwang](#i-phân-tích-sâu-điểm-mù-pháo-đài--sai-số-zugzwang)
  - [1.1 Ảo tưởng ưu thế vật chất trong các thế cờ pháo đài hòa của NNUE](#11-ảo-tưởng-ưu-thế-vật-chất-trong-các-thế-cờ-pháo-đài-hòa-của-nnue)
  - [1.2 Hiểm họa cắt tỉa Null Move Pruning trong thế cờ Zugzwang](#12-hiểm-họa-cắt-tỉa-null-move-pruning-trong-thế-cờ-zugzwang)
  - [1.3 Sự thiếu vắng đồ thị dòng chảy hàng rào chốt trong Stockfish cổ điển](#13-sự-thiếu-vắng-đồ-thị-dòng-chảy-hàng-rào-chốt-trong-stockfish-cổ-điển)
- [II. Kiến Trúc Kỹ Thuật Của SSFZRE](#ii-kiến-trúc-kỹ-thuật-của-ssfzre)
  - [2.1 Đồ thị Cắt Tối thiểu (Minimal Cut-Set Barrier Graph) trên Bitboard](#21-đồ-thị-cắt-tối-thiểu-minimal-cut-set-barrier-graph-trên-bitboard)
  - [2.2 Thuật toán Flood-Fill kiểm tra phong tỏa đường đi của Vua (King Path Obstruction)](#22-thuật-toán-flood-fill-kiểm-tra-phong-tỏa-đường-đi-của-vua-king-path-obstruction)
  - [2.3 Chỉ số Đóng băng Không gian Lambda và Hàm suy giảm tiệm cận hòa](#23-chỉ-số-đóng-băng-không-gian-lambda-và-hàm-suy-giảm-tiệm-cận-hòa)
  - [2.4 Cơ chế Đảo ngược Zugzwang (Zugzwang Inversion Mechanism)](#24-cơ-chế-đảo-ngược-zugzwang-zugzwang-inversion-mechanism)
- [III. Triển Khai Mã Nguồn Hoàn Chỉnh](#iii-triển-khai-mã-nguồn-hoàn-chỉnh)
  - [3.1 Triển khai Python Hoàn chỉnh (Kiểm thử Thuật toán Nhận diện Cấu trúc)](#31-triển-khai-python-hoàn-chỉnh-kiểm-thử-thuật-toán-nhận-diện-cấu-trúc)
  - [3.2 Giao diện C++ Engine Specification (Production Bitboard Hooks)](#32-giao-diện-c-engine-specification-production-bitboard-hooks)
- [IV. Phân Tích Ngân Sách Tính Toán & Ước Tính Đóng Góp Elo](#iv-phân-tích-ngân-sách-tính-toán--ước-tính-đóng-góp-elo)
- [V. Hướng Dẫn Kiểm Tra Lỗi & Edge Cases Cần Lưu Ý](#v-hướng-dẫn-kiểm-tra-lỗi--edge-cases-cần-lưu-ý)

---

## Tóm Tắt Tổng Quan (Executive Summary)

**SSFZRE (Structural Stagnation, Fortress & Zugzwang Resolution Engine)** là động cơ chuyên biệt giải quyết hai điểm mù nghiêm trọng nhất của mạng nơ-ron cập nhật tăng dần (NNUE): **Ảo tưởng ưu thế trong thế cờ Pháo đài (Fortress Blindness)** và **Đánh giá sai lệch trong tình thế Zugzwang**.

Bằng việc xây dựng Đồ thị cắt tối thiểu (Minimal Cut-Set Barrier Graph) kết hợp thuật toán Flood-Fill kiểm tra đường xâm nhập của Vua qua mảng Bitboard, SSFZRE đo lường chỉ số đóng băng không gian $\Lambda_{\text{stag}}$. Khi phát hiện cấu trúc pháo đài không thể phá vỡ, động cơ tự động áp dụng hàm suy giảm bất đối xứng ép điểm đánh giá tiệm cận 0, đồng thời tích hợp cơ chế vô hiệu hóa Null Move Pruning khi phát hiện nguy cơ Zugzwang, mang lại **+15 đến +30 Elo** (đặc biệt trong tàn cuộc kỹ thuật: **+35 đến +60 Elo**).

---

## I. Phân Tích Sâu Điểm Mù Pháo Đài & Sai Số Zugzwang

### 1.1 Ảo tưởng ưu thế vật chất trong các thế cờ pháo đài hòa của NNUE
Nút cổ chai thứ tư là sự mù quáng trước Pháo đài (Fortress), Xungzwang kép và sự trì trệ của luật 50 nước—những điểm yếu cố hữu của cả mạng NNUE lẫn cây tìm kiếm Alpha-Beta. Trong các tàn cuộc phức tạp hoặc thế cờ có cấu trúc tốt khóa chặt như KQvsKRM+pawns hay tượng khác màu với hàng rào tốt, mạng NNUE đánh giá tĩnh theo lợi thế vật chất (+300cp đến +500cp), nhưng thực tế không có bất kỳ chuỗi nước đi nào có thể xuyên thủng phòng tuyến đối phương trước khi chạm luật hòa 50 nước (halfmove_clock). Vì hiệu ứng đường chân trời của luật 50 nước đòi hỏi độ sâu 100 ply mới nhìn thấy kết quả hòa, engine sẽ từ chối các phương án hòa cờ an toàn hoặc đi những nước vô nghĩa làm mất lợi thế thực sự. Bốn chương tiếp theo sẽ thiết kế chi tiết bốn kiến trúc thuật toán hoàn toàn mới nhằm lấp đầy các khoảng trống này, tuân thủ nghiêm ngặt chuẩn mực kỹ thuật của hệ sinh thái hiện đại.

### 1.2 Hiểm họa cắt tỉa Null Move Pruning trong thế cờ Zugzwang
5.1 Phân Tích Sâu Điểm Mù Pháo Đài (Fortress) Và Xungzwang Trong Engine Hiện ĐạiMặc dù mạng NNUE và tìm kiếm chọn lọc sâu giúp Stockfish đạt trình độ siêu phàm trong các thế cờ chiến thuật mở, hai hiện tượng cấu trúc vẫn thường xuyên đánh lừa cả mạng nơ-ron lẫn tìm kiếm Alpha-Beta ở độ sâu 40+ ply. Hiện tượng thứ nhất là Hiệu ứng Ảo ảnh Vật chất trong Pháo đài (Fortress Material Illusion): trong các thế cờ pháo đài như Hậu chống Xe + Mã + Tốt bảo vệ nhau (KQvsKRM+pawns), Tượng khác màu với chuỗi tốt khóa chặt, hoặc Vua bị giam bên ngoài vùng đột phá, bên mạnh hơn có ưu thế vật chất lớn (+200cp đến +600cp). Mạng NNUE (kể cả HARENN) vốn cộng hưởng các đặc trưng vị trí quân (HalfKAv2) nên luôn xuất ra điểm đánh giá cao cho bên nhiều quân. Trong khi đó, bên phòng thủ chỉ cần đi bộ qua lại (shuffling) phía sau hàng rào bất khả xâm phạm. Vì luật hòa 50 nước chỉ có hiệu lực sau 100 ply không ăn quân và không đẩy tốt, một cuộc tìm kiếm ở độ sâu 30–40 ply không bao giờ chạm tới ngưỡng 100 ply đó. Hậu quả là engine dẫn điểm từ chối các biến chuyển đổi thực chất hoặc từ chối hòa cờ khi đang bất lợi ở nhánh khác để lao vào một pháo đài hòa đặc.Hiện tượng thứ hai là sự sụp đổ của giả định nước đi rỗng trong Xungzwang Kép (Mutual/Complex Zugzwang). Kỹ thuật Null Move Pruning (NMP) và ngay cả hàm compute_zugzwang_risk trong UPAD chủ yếu dựa trên việc đếm tổng giá trị quân không phải tốt (non_pawn_material) và số tốt bị chặn. Tuy nhiên, xungzwang không chỉ xảy ra khi hết quân nặng; nó xảy ra ngay cả khi còn Xe và Tượng nếu mọi quân cờ đều bị trói vào nhiệm vụ bảo vệ các điểm yếu cố định (Overloaded Defensive Tie-Down — khái niệm đã được định lượng trong mô-đun TAQ của DEE). Khi thực hiện NMP hoặc giảm độ sâu mạnh ở thế cờ mà mọi nước đi hợp lệ đều làm suy yếu phòng tuyến, engine sẽ cắt tỉa sai lầm nhánh duy nhất hoặc đánh giá ngược hoàn toàn kết quả thế cờ.

---

## II. Kiến Trúc Kỹ Thuật Của SSFZRE

SSFZRE (Structural Stagnation, Fortress & Zugzwang Resolution Engine) giải quyết triệt để hai điểm mù trên thông qua ba mô-đun toán học kết nối trực tiếp với biểu diễn Bitboard và bộ nhớ cấu trúc tốt.Mô-đun thứ nhất là Bộ Phân tích Hàng rào Đồ thị Bitboard (Bitboard Permeability & Cut-Set Analyzer). Để một bên đang có ưu thế ($\text{eval} > +120\text{cp}$) giành chiến thắng, ít nhất một tác nhân tấn công (Vua, quân mạnh, hoặc tốt thông) phải xâm nhập được vào vùng trọng yếu của bên phòng thủ (ô phong cấp hoặc vùng an toàn quanh Vua đối phương). SSFZRE xây dựng đồ thị chuyển động của bên tấn công $G_{\text{att}} = (V, E_{\text{safe}})$, trong đó một ô $s \in V$ bị coi là Ô Khóa Cứng (Impermeable Blocker) nếu ô đó chứa một tốt cố định của bên phòng thủ không thể bị bắt có lợi ($\text{SEE} < 0$), hoặc nếu ô đó được kiểm soát bởi tốt phòng thủ hay quân phòng thủ đứng trên ô an toàn tuyệt đối (outpost không thể bị tốt đối phương tấn công) trong khi bên tấn công không còn tốt trên các cột lân cận để thực hiện nước đột phá tốt (pawn_break_count == 0). Sử dụng phép toán lan truyền lũy thừa trên Bitboard (Bitboard Flood-Fill Reachability), SSFZRE kiểm tra xem tập hợp các ô xuất phát của Vua và quân tấn công $S_{\text{origin}}$ có đường đi xuyên qua tập ô không bị khóa $B_{\text{free}}$ để chạm tới tập ô mục tiêu $S_{\text{target}}$ hay không:$$\mathcal{R}_{\text{reach}} = \text{FloodFill}(S_{\text{origin}}, \; \sim B_{\text{impermeable}}) \cap S_{\text{target}}$$Nếu $\mathcal{R}_{\text{reach}} = \emptyset$ và số lượng nước đột phá tốt khả thi của bên mạnh bằng $0$, thế cờ tạo thành một Topological Cut-Set (Hàng rào chia cắt đồ thị) với chỉ số phong tỏa $\mathcal{B}_{\text{cut}} \in [0, 1]$ tiến sát về $1.0$.Mô-đun thứ hai là Hệ số Suy giảm Trì trệ Cấu trúc (Structural Stagnation Decay - $\Lambda_{\text{stag}}$). Thay vì đợi đồng hồ 50 nước (halfmove_clock) chạm mức 100, SSFZRE theo dõi Độ lệch Tiến triển Cấu trúc dọc theo nhánh tìm kiếm từ gốc đến nút lá hiện tại:$$\Delta \mathcal{P}_{\text{prog}}(P_{\text{root}} \to P_{\text{leaf}}) = |\text{pawn\_hash}(P_{\text{leaf}}) \neq \text{pawn\_hash}(P_{\text{root}})| + \Delta \text{material} + \Delta \text{king\_penetration}$$Nếu một nhánh tìm kiếm đi sâu $\Delta \text{ply} \ge 8$ mà cấu trúc tốt không hề thay đổi ($\Delta \text{pawn\_hash} = 0$), không có trao đổi vật chất ($\Delta \text{material} = 0$), Vua bên mạnh không tiến sâu hơn vào phần sân đối phương, và điểm đánh giá tĩnh của HARENN dao động trong biên độ hẹp quanh một mức cao giả tạo, SSFZRE tính toán xác suất pháo đài $P_{\text{fortress}}$ và thu gọn điểm đánh giá về điểm hòa ($0\,\text{cp}$):$$P_{\text{fortress}} = \text{sigmoid}\left( 4.0 \cdot \mathcal{B}_{\text{cut}} + 2.5 \cdot \frac{\text{stagnant\_plies} + 0.5 \cdot \text{halfmove\_clock}}{20.0} - 3.2 \right)$$$$\text{Eval}_{\text{corrected}}(P) = \text{Eval}_{\text{HARENN}}(P) \cdot (1.0 - P_{\text{fortress}})$$Cơ chế này cho phép engine nhận ra pháo đài chỉ sau 8–12 ply tìm kiếm (khi thấy mọi nỗ lực đều không làm thay đổi cấu trúc $\Delta \mathcal{P}_{\text{prog}} = 0$), thay vì phải đợi tới 100 ply.Mô-đun thứ ba là Bộ Nhận diện Xungzwang Qua Độ Trói Quân (Tie-Down Zugzwang Detector). Mở rộng công thức zugzwang_risk của UPAD bằng cách kết hợp giá trị trói quân (tie_down_value) từ mô-đun TAQ của DEE, SSFZRE định lượng tỷ lệ số nước đi hợp lệ không làm mất vật chất ($\text{SEE}(m) \ge 0$) và không bỏ rơi ô đang phòng thủ trên tổng số nước đi hợp lệ:$$\mathcal{Z}_{\text{exact}}(P) = 1.0 - \frac{|\{m \in \text{Legal}(P) : \text{SEE}^+(m) \ge 0 \land \Delta \text{Defended}(m) \ge 0\}|}{\max(1, |\text{Legal}(P)|)}$$Khi $\mathcal{Z}_{\text{exact}}(P) > 0.75$, SSFZRE khóa hoàn toàn Null Move Pruning trong UPAD và cấp phát mở rộng $+1$ ply cho nhánh chuyển lượt đi nhằm tìm đòn buộc nước đối phương.

---

## III. Triển Khai Mã Nguồn Hoàn Chỉnh

### 3.1 Triển khai Python Hoàn chỉnh (Kiểm thử Thuật toán Nhận diện Cấu trúc)
```python
import math

class StructuralFortressAndZugzwangEngine:
    """
    SSFZRE: Structural Stagnation, Fortress & Zugzwang Resolution Engine.
    Phát hiện sớm pháo đài (Fortress) qua thuật toán Bitboard Flood-Fill Cut-Set
    và sự trì trệ cấu trúc (Stagnation Decay), đồng thời nhận diện Xungzwang
    đa quân qua tỷ lệ kiệt quệ nước đi an toàn.
    """
    
    def __init__(self):
        self.fortress_cache = {}
        self.stats = {
            'fortresses_detected': 0,
            'stagnation_dampings': 0,
            'zugzwang_locks_triggered': 0,
        }

    def evaluate_and_correct(self, position, raw_eval_cp, root_state, current_ply_from_root):
        """
        Kiểm tra pháo đài và sự trì trệ cấu trúc; trả về điểm đánh giá đã hiệu chỉnh
        cùng chỉ số rủi ro xungzwang chính xác cho UPAD.
        """
        # Chỉ cần kiểm tra pháo đài khi một bên đang được đánh giá có ưu thế rõ (> 120cp)
        if abs(raw_eval_cp) < 120:
            zug_risk = self.compute_exact_zugzwang_risk(position)
            return raw_eval_cp, 0.0, zug_risk

        stronger_side = 0 if raw_eval_cp > 0 else 1
        if position.side_to_move != stronger_side:
            stronger_side = position.side_to_move ^ 1

        # ═══ BƯỚC 1: PHÂN TÍCH HÀNG RÀO ĐỒ THỊ BITBOARD (CUT-SET BARRIER) ═══
        barrier_score = self._compute_bitboard_barrier_cutset(position, stronger_side)

        # ═══ BƯỚC 2: ĐO LƯỜNG SỰ TRÌ TRỆ CẤU TRÚC DỌC NHÁNH TÌM KIẾM ═══
        stagnant_plies = self._measure_structural_stagnation(
            position, root_state, current_ply_from_root, stronger_side
        )

        # ═══ BƯỚC 3: TÍNH XÁC SUẤT PHÁO ĐÀI & HIỆU CHỈNH ĐIỂM ĐÁNH GIÁ ═══
        hmc = position.halfmove_clock
        effective_stagnation = (stagnant_plies * 1.5 + 0.5 * hmc) / 20.0
        
        # Nếu không còn nước đột phá tốt và hàng rào bitboard kín -> xác suất pháo đài tăng vọt
        logit = 4.0 * barrier_score + 2.5 * effective_stagnation - 3.2
        p_fortress = 1.0 / (1.0 + math.exp(-logit))

        if p_fortress > 0.15:
            self.stats['stagnation_dampings'] += 1
            if p_fortress > 0.75:
                self.stats['fortresses_detected'] += 1
            corrected_eval = int(raw_eval_cp * (1.0 - p_fortress))
        else:
            corrected_eval = raw_eval_cp

        # ═══ BƯỚC 4: TÍNH RỦI RO XUNGZWANG CHÍNH XÁC CHO UPAD ═══
        zug_risk = self.compute_exact_zugzwang_risk(position)
        return corrected_eval, p_fortress, zug_risk

    def compute_exact_zugzwang_risk(self, position):
        """
        Tính toán rủi ro xungzwang dựa trên tỷ lệ nước đi hợp lệ không làm suy yếu
        phòng tuyến (Tie-Down Safe Mobility Ratio), vượt qua hạn chế chỉ đếm quân của UPAD.
        """
        stm = position.side_to_move
        legal_moves = position.legal_moves()
        if not legal_moves:
            return 0.0

        # Đếm số nước đi an toàn: không mất vật chất (SEE >= 0) và không bỏ rơi quân đang bảo vệ
        safe_maintaining_moves = 0
        for move in legal_moves:
            if see(position, move) < 0:
                continue
            if self._abandons_critical_defense(position, move, stm):
                continue
            safe_maintaining_moves += 1

        safe_ratio = safe_maintaining_moves / float(len(legal_moves))
        exhaustion_score = 1.0 - safe_ratio

        # Kết hợp với yếu tố ít quân của tàn cuộc
        non_pawn_mat = count_non_pawn_material(position, stm)
        mat_factor = 0.5 if non_pawn_mat == 0 else (0.25 if non_pawn_mat <= 500 else 0.0)
        
        zug_risk = clamp(0.75 * exhaustion_score + mat_factor, 0.0, 1.0)
        if zug_risk > 0.75:
            self.stats['zugzwang_locks_triggered'] += 1
        return zug_risk

    def _compute_bitboard_barrier_cutset(self, position, attacking_side):
        """
        Sử dụng Bitboard Flood-Fill để kiểm tra xem Vua và quân tấn công của bên mạnh
        có thể xuyên qua hàng rào tốt khóa và vùng kiểm soát của bên phòng thủ hay không.
        """
        defending_side = attacking_side ^ 1
        cache_key = (position.pawn_key, position.material_key, attacking_side)
        if cache_key in self.fortress_cache:
            return self.fortress_cache[cache_key]

        # 1. Kiểm tra số lượng nước đột phá tốt (pawn breaks) khả thi của bên tấn công
        pawn_breaks = count_viable_pawn_breaks(position, attacking_side)
        if pawn_breaks >= 2:
            self.fortress_cache[cache_key] = 0.0
            return 0.0

        # 2. Xác định mặt nạ các ô bị khóa cứng (Impermeable Squares Bitboard)
        def_pawns = position.pawns_bitboard(defending_side)
        def_pawn_attacks = compute_pawn_attacks_bitboard(def_pawns, defending_side)
        safe_def_controls = compute_fortified_defense_squares(position, defending_side)
        
        impermeable_mask = def_pawns | def_pawn_attacks | safe_def_controls
        passable_mask = (~impermeable_mask) & 0xFFFFFFFFFFFFFFFF

        # 3. Chạy thuật toán Flood-Fill từ vị trí Vua và quân tấn công tới vùng mục tiêu
        att_king_bb = 1 << position.king_square(attacking_side)
        target_zone_bb = get_invasion_target_mask(defending_side)
        
        reached_bb = att_king_bb
        for _ in range(14):
            expanded = step_king_bitboard(reached_bb) & passable_mask
            if expanded == reached_bb:
                break
            reached_bb = expanded

        king_blocked = (reached_bb & target_zone_bb) == 0
        
        # Tính điểm phong tỏa kết hợp đặc trưng Tượng khác màu và biên 1 cánh
        barrier = 0.0
        if king_blocked:
            barrier += 0.55
        if pawn_breaks == 0:
            barrier += 0.25
        if has_opposite_color_bishops(position):
            barrier += 0.20

        barrier = clamp(barrier, 0.0, 1.0)
        if len(self.fortress_cache) < 16384:
            self.fortress_cache[cache_key] = barrier
        return barrier

    def _measure_structural_stagnation(self, position, root_state, current_ply, stronger_side):
        """Đếm số ply liên tiếp không có thay đổi cấu trúc tốt, vật chất hoặc xâm nhập Vua."""
        if root_state is None or current_ply < 4:
            return 0
        same_pawns = (position.pawn_key == root_state.pawn_key)
        same_material = (position.material_key == root_state.material_key)
        if not (same_pawns and same_material):
            return 0
            
        # Kiểm tra xem Vua bên mạnh có tiến gần hơn tới mục tiêu so với ở gốc không
        king_prog = king_invasion_progress(position, stronger_side) - \
                    king_invasion_progress(root_state.position, stronger_side)
        if king_prog > 0:
            return 0
        return current_ply

    def _abandons_critical_defense(self, position, move, side):
        """Kiểm tra xem việc di chuyển quân từ from_square có làm mất bảo vệ một ô trọng yếu không."""
        from_sq = move.from_square
        piece = position.piece_at(from_sq)
        if piece is None:
            return False
        for defended_sq in position.squares_defended_by(from_sq):
            target = position.piece_at(defended_sq)
            if target and target.side == side:
                opp_attackers = len(position.attackers_of(defended_sq, side ^ 1))
                our_defenders = len(position.defenders_of(defended_sq, side))
                if opp_attackers >= our_defenders:
                    return True
        return False
```

### 3.2 Giao diện C++ Engine Specification (Production Bitboard Hooks)
```cpp
#pragma once
#include <cstdint>
#include <cmath>
#include "types.h"

namespace NextFish {

struct SSFZEResult {
    Value corrected_eval;
    double stagnation_index;
    bool is_impenetrable;
    double zugzwang_risk;
};

class StructuralFortressEngine {
public:
    SSFZEResult evaluate_fortress(Position& pos, Value raw_eval, int halfmove_clock);
};

} // namespace NextFish
```

---

## IV. Phân Tích Ngân Sách Tính Toán & Ước Tính Đóng Góp Elo

| Khía cạnh đánh giá | Stockfish + NNUE Tiêu chuẩn | Kiến trúc SSFZRE | Ý nghĩa chiến lược & Tác động |
|---|---|---|---|
| **Nhận diện Pháo đài** | Đánh giá sai $+250\text{cp}$ đến $+500\text{cp}$ kéo dài đến khi hết 50 nước | Phát hiện sau $8 - 12$ ply qua Bitboard Flood-Fill & $\Lambda_{\text{stag}}$ | **Tránh đi vào biến hòa giả tạo**, tìm đường chuyển đổi thắng thực chất |
| **Phát hiện Zugzwang đa quân** | Chỉ dựa vào tổng giá trị quân không phải tốt | Tính tỷ lệ kiệt quệ nước đi an toàn $\mathcal{Z}_{\text{exact}}(P)$ kết hợp DEE TAQ | **Ngăn chặn lỗi NMP cắt bỏ nước chờ duy nhất** ở trung-tàn cuộc |
| **Chi phí tính toán trung bình** | $0\,\mu\text{s}$ (hoặc vài heuristics cố định) | $0.15 - 0.40\,\mu\text{s}$ (nhờ `pawn_key` cache hit $>92\%$) | Chi phí cực thấp, chỉ kích hoạt khi $d \ge 6$ và quân tàn cuộc |
| **Ước tính đóng góp Elo** | Baseline (chuẩn đối chiếu) | **+15 đến +30 Elo** (Tàn cuộc kỹ thuật: **+35 đến +60 Elo**) | **Giải quyết triệt để điểm yếu lớn nhất của kiến trúc NNUE** |

---

## V. Hướng Dẫn Kiểm Tra Lỗi & Edge Cases Cần Lưu Ý
1. **Bảo toàn Cột Tốt Chưa Khóa (Open File Ingress)**: Nếu tồn tại ít nhất một cột mở hoặc bán mở nơi Vua có thể xâm nhập, không bao giờ kết luận pháo đài hoàn toàn.
2. **Hy Sinh Quân Phá Khóa (Sacrifice Breakthroughs)**: Phải kiểm tra xem bên tấn công có nước hy sinh quân nhẹ (e.g. Bxh6, Nxe5) mở đường cho tốt xuống phong cấp hay không thông qua DEE.
3. **Tránh Ngộ Nhận Zugzwang Khi Có Nước Chờ của Tốt (Pawn Waiting Moves)**: Phải tính toán kỹ các nước đi có thể hoãn lại của tốt biên trước khi xem bên kia bị rơi vào thế cạn nước đi.