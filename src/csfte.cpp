#include "csfte.h"
#include <cmath>
#include <algorithm>

namespace NextFish {

CSFTEEngine GlobalCSFTE;

double CSFTEEngine::compute_prior_singularity(const Position& pos, Move candidate_move, Move second_best_move,
                                            const HARENNOutput& harenn_out) const {
    double cand_mcs = harenn_out.get_mcs(pos, candidate_move);
    double second_mcs = second_best_move.is_valid() ? harenn_out.get_mcs(pos, second_best_move) : 0.10;
    double mcs_gap = std::clamp(cand_mcs - second_mcs, 0.0, 1.0);
    return std::clamp(0.65 * mcs_gap + 0.35 * (cand_mcs > 0.60 ? 1.0 : 0.0), 0.0, 1.0);
}

CSFTEEvalResult CSFTEEngine::evaluate(const Position& pos, Move candidate_move, int depth, int ply,
                                     Value beta, Value ref_score, const HARENNOutput& harenn_out,
                                     Move second_best_move, int path_extensions) const {
    CSFTEEvalResult res;

    // Never extend at root, at shallow depths, or if path extension limit reached
    if (ply == 0 || depth < 6 || path_extensions >= 6) {
        return res;
    }

    double s_prior = compute_prior_singularity(pos, candidate_move, second_best_move, harenn_out);

    // Fast-path skips
    if (s_prior <= 0.18) {
        res.action = CSFTE_FAST_PATH_SKIP;
        return res;
    }

    // Excluded search verification required for all candidate singular moves
    res.action = CSFTE_RUN_EXCLUDED_SEARCH;
    double margin_mult = (1.2 + 2.2 * harenn_out.tactical_complexity - 0.8 * harenn_out.horizon_risk) * (1.25 - 0.5 * s_prior);
    res.singular_beta = static_cast<Value>(ref_score - depth * std::clamp(margin_mult, 0.8, 3.5));
    res.singular_depth = std::max(1, (depth - 1) / 2);

    return res;
}

void CSFTEEngine::apply_fractional_extension(int base_depth, double delta_ext,
                                           double& accumulated_ext, int& out_int_ext,
                                           int& out_new_depth) {
    double total_acc = accumulated_ext + delta_ext;
    if (total_acc >= 1.0) {
        out_int_ext = static_cast<int>(std::floor(total_acc));
        accumulated_ext = total_acc - out_int_ext;
    } else if (total_acc <= -1.0) {
        out_int_ext = static_cast<int>(std::ceil(total_acc));
        accumulated_ext = total_acc - out_int_ext;
    } else {
        out_int_ext = 0;
        accumulated_ext = total_acc;
    }
    out_new_depth = std::max(1, base_depth + out_int_ext);
}

} // namespace NextFish
