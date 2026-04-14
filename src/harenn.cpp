#include "harenn.h"
#include "position.h"
#include "bitboard.h"
#include "dee.h"
#include "dee_cache.h"
#include <algorithm>

namespace Stockfish {
namespace HARENN {

int GuidanceProvider::compute_reduction_adjustment(const Position& pos,
                                               Depth           depth,
                                               Move            m,
                                               int             currentR) {
    (void) depth;
    (void) currentR;

    if (!m) return 0;

    Color us = pos.side_to_move();
    Square enemyKing = pos.square<KING>(~us);
    Square to = m.to_sq();

    // Protect moves that land near the enemy king
    int dist = std::max(std::abs(file_of(to) - file_of(enemyKing)), 
                        std::abs(rank_of(to) - rank_of(enemyKing)));

    if (dist <= 2)
        return -128; // Small reduction decrease for king-proximity moves

    return 0;
}

int GuidanceProvider::compute_aspiration_delta(const Position& pos,
                                               int             iteration,
                                               int             currentDelta) {
    // In high tension positions, broaden the aspiration window to avoid costly re-searches.
    // This is effective for maintaining stability in tactical chaos.
    if (iteration < 6) return currentDelta;

    int tension = DEE::get_tension(pos);
    if (tension > 50)
        return currentDelta + (tension / 4);

    return currentDelta;
}

} // namespace HARENN
} // namespace Stockfish
