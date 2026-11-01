#pragma once

#include "types.h"
#include "position.h"
#include "movegen.h"
#include "dee.h"

namespace NextFish {

class HAMOEngine {
public:
    HAMOEngine();
    void clear();

    // Generate and order moves for the given position
    void generate_and_order(const Position& pos, MoveList& ordered_moves,
                            Move tt_move, Move prev_move, int ply,
                            double tactical_complexity = 0.5) const;

    // Update history, killers, and counter moves upon beta cutoff
    void update_on_cutoff(const Position& pos, Move move, int depth, int ply,
                          Move prev_move, const Move* tried_quiets, int tried_count);

    int get_history(const Position& pos, Move m, Move prev_move) const {
        Square from = m.from();
        Square to = m.to();
        Color us = pos.side_to_move();
        int hist = butterfly_history[us][from][to];
        Piece moving_pc = pos.piece_on(from);
        if (prev_move.is_valid()) {
            Piece prev_pc = pos.piece_on(prev_move.to());
            if (prev_pc < 12 && moving_pc < 12) {
                hist += 2 * continuation_history[prev_pc][prev_move.to()][moving_pc][to];
            }
        }
        int p_class = static_cast<int>(pos.pawn_hash_key() & 0x3F);
        if (moving_pc < 12) {
            hist += pawn_history[p_class][moving_pc][to];
        }
        return hist;
    }

    Move get_counter_move(const Position& pos, Move prev_move) const {
        if (!prev_move.is_valid()) return MOVE_NONE;
        Piece prev_pc = pos.piece_on(prev_move.to());
        if (prev_pc < 12) {
            return counter_moves[prev_pc][prev_move.to()];
        }
        return MOVE_NONE;
    }

    int get_capture_history(Piece pc, Square to, PieceType victim) const {
        if (pc < 12 && victim < 6) {
            return capture_history[pc][to][victim];
        }
        return 0;
    }

private:
    int score_move(const Position& pos, Move m, Move tt_move, Move prev_move,
                   int ply, double tactical_complexity) const;

    // Killer moves [MAX_PLY][2]
    Move killer_moves[MAX_PLY][2];

    // Counter moves [16][64]
    Move counter_moves[16][64];

    // Butterfly history [COLOR_NB][64][64]
    int butterfly_history[COLOR_NB][64][64];

    // Continuation history [16][64][16][64]
    int continuation_history[16][64][16][64];

    // Pawn structure context history [64][16][64]
    int pawn_history[64][16][64];

    // Capture history [16][64][6]
    int capture_history[16][64][6];
};

extern HAMOEngine GlobalHAMO;

} // namespace NextFish
