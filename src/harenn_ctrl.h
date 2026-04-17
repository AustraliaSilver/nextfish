#ifndef HARENN_CTRL_H_INCLUDED
#define HARENN_CTRL_H_INCLUDED

#include "types.h"
#include "position.h"
#include "harenn.h"

namespace Stockfish {

namespace HARENN {

class Controller {
public:
    static void init();
    
    // Phân tích AI tích hợp (Tau, Rho, Rs, Eval)
    static EvalResult get_analysis(const Position& pos);

    // Điều phối cắt tỉa (LMR) dựa trên AI
    static int get_smart_reduction(const Position& pos, Depth depth, Move m, int moveCount, int baseR);

    // Điều chỉnh cửa sổ tìm kiếm (Aspiration)
    static int adjust_aspiration(const Position& pos, int delta);

    // Thưởng điểm nước đi (Dành cho các bản test tương lai)
    static int get_move_bonus(const Position& pos, Move m);

    // Điều phối Quiescence Search (Đồng thuận AI-Engine)
    static int get_qs_tactical_adjustment(const Position& pos, int standPat);
};

} // namespace HARENN

} // namespace Stockfish

#endif // HARENN_CTRL_H_INCLUDED
