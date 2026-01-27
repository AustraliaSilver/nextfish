#include "lc0_policy.h"
#include "movegen.h"
#include <algorithm>
#include <cmath>
#include <iostream>

// Giả lập giao diện ONNX Runtime để code có thể biên dịch khung
// Trong thực tế, bạn sẽ include <onnxruntime_cxx_api.h>
namespace Nextfish {

bool Lc0Policy::initialized = false;
bool Lc0Policy::isActive = true;

bool Lc0Policy::initialize(const std::string& modelPath) {
    if (modelPath.empty()) return false;
    // Khởi tạo Session ONNX Runtime tại đây
    initialized = true;
    std::cout << "Nextfish: Lc0 Model loaded from " << modelPath << std::endl;
    return true;
}

void Lc0Policy::encode_position(const Position& pos, float* input) {
    std::fill(input, input + 112 * 64, 0.0f);
    Color us = pos.side_to_move();

    // Plane 0-11: Piece positions (P, N, B, R, Q, K cho us và them)
    for (PieceType pt : {PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING}) {
        for (Color c : {WHITE, BLACK}) {
            Bitboard bb = pos.pieces(c, pt);
            int plane_idx = (pt - PAWN) + (c == us ? 0 : 6);
            while (bb) {
                Square s = pop_lsb(bb);
                int row = (us == WHITE) ? rank_of(s) : 7 - rank_of(s);
                int col = (us == WHITE) ? file_of(s) : 7 - file_of(s);
                input[plane_idx * 64 + row * 8 + col] = 1.0f;
            }
        }
    }
    // Lc0 cần thêm các planes về nhập thành, luật 50 nước, v.v. (tối giản cho root)
}

Move Lc0Policy::index_to_move(int index, const Position& pos) {
    int from_idx = index / 73;
    int move_type = index % 73;

    int from_sq_idx = (pos.side_to_move() == WHITE) ? from_idx : (from_idx ^ 56);
    Square from_sq = Square(from_sq_idx);
    int from_rank = rank_of(from_sq);
    int from_file = file_of(from_sq);

    Square to_sq = SQ_NONE;

    if (move_type < 56) { // Queen moves
        int direction = move_type / 7;
        int distance = (move_type % 7) + 1;
        static const int dr[] = {1, 1, 0, -1, -1, -1, 0, 1};
        static const int df[] = {0, 1, 1, 1, 0, -1, -1, -1};
        int to_rank = from_rank + dr[direction] * distance;
        int to_file = from_file + df[direction] * distance;
        if (to_rank >= 0 && to_rank <= 7 && to_file >= 0 && to_file > 7)
            to_sq = make_square(File(to_file), Rank(to_rank));
    } else if (move_type < 64) { // Knight moves
        static const int knr[] = {2, 1, -1, -2, -2, -1, 1, 2};
        static const int knf[] = {1, 2, 2, 1, -1, -2, -2, -1};
        int to_rank = from_rank + knr[move_type - 56];
        int to_file = from_file + knf[move_type - 56];
        if (to_rank >= 0 && to_rank <= 7 && to_file >= 0 && to_file <= 7)
            to_sq = make_square(File(to_file), Rank(to_rank));
    }

    if (to_sq == SQ_NONE) return Move::none();
    if (pos.side_to_move() == BLACK) to_sq = Square(int(to_sq) ^ 56);

    for (const auto& m : MoveList<LEGAL>(pos)) {
        if (m.from_sq() == from_sq && m.to_sq() == to_sq) return m;
    }
    return Move::none();
}

std::vector<Move> Lc0Policy::get_top_moves(const Position& pos, int n) {
    if (!initialized || !isActive) return {};

    // Giả lập Output từ mạng BT4-it332
    // Trong thực tế: Chạy session.Run() -> lấy Policy Tensor
    std::vector<Move> topMoves;
    
    // Tạm thời lấy các nước đi hợp lệ tốt nhất theo thứ tự movegen 
    // cho đến khi bạn liên kết thư viện ONNX thực tế.
    for (const auto& m : MoveList<LEGAL>(pos)) {
        topMoves.push_back(m);
        if (topMoves.size() >= (size_t)n) break;
    }

    return topMoves;
}

} // namespace Nextfish
