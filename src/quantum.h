#ifndef QUANTUM_H_INCLUDED
#define QUANTUM_H_INCLUDED

#include "types.h"
#include <cmath>

namespace Stockfish::Quantum {

    // Quantum Parameters - Hyperparameters for Asymmetric Aggression
    struct Params {
        // --- Time Dilation (Quản lý thời gian bất đối xứng) ---
        double whiteTimeBonus = 1.10; 
        double blackPanicBonus = 1.15; 
        double blackBaseBonus = 1.0;  

        // Complexity Boost
        double complexityFactor = 1.08; 

        // --- Entangled Evaluation (Đánh giá vướng víu) ---
        int contempt = 0; 
        int optimismBase = 142; 
    };

    inline Params params;

    // Weighted King Danger detection (ShashChess inspired)
    inline bool king_danger(const Position& pos, Color c) noexcept {
        const Square ksq = pos.square<KING>(c);
        Bitboard attackers = pos.attackers_to(ksq) & pos.pieces(~c);
        if (!attackers) return false;

        int weight = 0;
        if (attackers & pos.pieces(~c, QUEEN)) weight += 4;
        if (attackers & pos.pieces(~c, ROOK))  weight += 3;
        
        // Neighborhood threat
        Bitboard neighborhood = pos.attacks_by<KING>(c);
        weight += popcount(attackers | (pos.pieces(~c) & neighborhood));

        return weight >= 5;
    }

    // Cảm biến nhận diện Pháo đài (Fortress)
    inline bool is_fortress(const Position& pos) {
        const Color us = pos.side_to_move();
        const Color them = ~us;
        
        // Điều kiện cơ bản: Rule50 cao, ít quân, không có Hậu đối phương
        if (pos.rule50_count() < 20 || pos.count<ALL_PIECES>() > 16 || pos.count<QUEEN>(them) > 0)
            return false;

        const Square ourKing = pos.square<KING>(us);
        const Square theirKing = pos.square<KING>(them);

        // Re và quân phòng thủ đứng gần nhau
        if (distance(ourKing, theirKing) < 4) return false;

        // Có ít nhất 3 bộ đôi pawn bảo vệ nhau
        Bitboard ourPawns = pos.pieces(us, PAWN);
        int shieldCount = popcount(ourPawns & (shift<NORTH>(ourPawns) | shift<SOUTH>(ourPawns)));
        
        return shieldCount >= 3;
    }

    // Cảm biến nhận diện thế trận hy sinh
    inline bool is_sacrificial(const Position& pos) {
        // Kiểm tra nhanh xem có quân nào bị tấn công bởi quân giá trị thấp hơn không
        for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) {
            Bitboard pieces = pos.pieces(pos.side_to_move(), pt);
            while (pieces) {
                Square s = pop_lsb(pieces);
                if (pos.attackers_to(s, ~pos.side_to_move()) & pos.pieces(~pos.side_to_move(), PAWN))
                    return true;
            }
        }
        return false;
    }

    // Hàm tính toán hệ số thời gian bằng số nguyên để tối ưu NPS
    inline int time_scale_int(const Position& pos, Color us, Value bestValue, Value prevBestValue, bool bestMoveChanged) {
        int scale = 100; // 100 tương đương 1.0x

        // 1. Phân bổ thời gian bất đối xứng
        if (us == WHITE) {
            scale = 110; // 1.10x
        } else {
            // Panic bonus cho quân Đen khi gặp khó khăn
            if (bestValue < -30 && bestValue > -200) { 
                scale = 125; // 1.25x
            } else {
                scale = 105; // 1.05x
            }
        }

        // 2. Kiểm tra Entropy (Biến động điểm số)
        int diff = std::abs(bestValue - prevBestValue);
        if (diff > 20 || bestMoveChanged) {
            scale = (scale * 108) / 100;
            if (diff > 50) scale = (scale * 105) / 100; 
        }

        // 3. Cảm biến Vua (Phòng thủ)
        if (king_danger(pos, us)) {
            scale = (scale * 120) / 100;
        }
        
        // 4. Tấn công
        if (king_danger(pos, ~us)) {
            scale = (scale * 110) / 100;
        }

        return scale;
    }
}

#endif
