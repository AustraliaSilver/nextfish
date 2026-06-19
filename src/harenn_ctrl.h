#ifndef HARENN_CTRL_H_INCLUDED
#define HARENN_CTRL_H_INCLUDED

#include "types.h"
#include "position.h"
#include "harenn.h"

#include <string>

namespace Stockfish {

class OptionsMap;

namespace HARENN {

class Controller {
public:
    static void init();

    // Load tunable parameters from UCI options (call at start of search)
    static void refresh_params(const OptionsMap& options);
    
    // Phân tích AI tích hợp (Tau, Rho, Rs, Eval)
    static EvalResult get_analysis(const Position& pos, NumaReplicatedAccessToken numaToken);
    static std::pair<float, float> get_rho_and_rs(const Position& pos, NumaReplicatedAccessToken numaToken);

    // Điều phối cắt tỉa (LMR) dựa trên AI
    static int get_smart_reduction(const Position& pos, Depth depth, Move m, int moveCount, int baseR, Value staticEval, Value rootScore);

    // Điều chỉnh cửa sổ tìm kiếm (Aspiration)
    static int adjust_aspiration(const Position& pos, int delta);

    // Thưởng điểm nước đi (Dành cho các bản test tương lai)
    static int get_move_bonus(const Position& pos, Move m);

    // AI-based selective depth extension
    static int get_search_extension(const Position& pos, Move m, Depth depth, bool givesCheck, NumaReplicatedAccessToken numaToken);

    // Điều phối Quiescence Search (Đồng thuận AI-Engine)
    static int get_qs_tactical_adjustment(const Position& pos, int standPat);

    // Điều phối thời gian (Time management)
    static int get_time_multiplier(const Position& pos);
};

} // namespace HARENN

} // namespace Stockfish

#endif // HARENN_CTRL_H_INCLUDED
