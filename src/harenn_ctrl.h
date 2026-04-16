#ifndef HARENN_CTRL_H_INCLUDED
#define HARENN_CTRL_H_INCLUDED

#include "types.h"
#include "position.h"
#include <vector>

namespace Stockfish {

namespace HARENN {

// Đặc trưng chiến thuật được trích xuất từ Bitboard (Theo mục IV tài liệu HARENN.md)
struct TacticalProfile {
    int attack_density;
    int king_ring_pressure;
    int hanging_pieces_count;
    int mobility_score;
    int structural_tension;
    float nn_tau; // Độ phức tạp từ mạng thần kinh
};

class Controller {
public:
    static void init();
    
    // Phân tích toàn diện thế cờ để đưa ra quyết định tìm kiếm
    static TacticalProfile analyze(const Position& pos);

    // Tính toán mức độ cắt giảm (Reduction) thông minh
    static int get_smart_reduction(const Position& pos, Depth depth, Move m, int moveCount, int baseR);

    // Điều chỉnh cửa sổ tìm kiếm (Aspiration) dựa trên độ biến động
    static int adjust_aspiration(const Position& pos, int delta);

    // Thưởng điểm sắp xếp nước đi (Move Ordering)
    static int get_move_bonus(const Position& pos, Move m);

private:
    // Trích xuất 100 đặc trưng thô để nạp vào mạng hoặc dùng làm Heuristic
    static std::vector<float> extract_features(const Position& pos);
};

} // namespace HARENN

} // namespace Stockfish

#endif // HARENN_CTRL_H_INCLUDED
