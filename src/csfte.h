#pragma once

#include "types.h"
#include "position.h"
#include "harenn.h"

namespace NextFish {

enum CSFTEAction {
    CSFTE_NONE,
    CSFTE_RUN_EXCLUDED_SEARCH,
    CSFTE_FAST_PATH_EXTEND,
    CSFTE_FAST_PATH_SKIP,
    CSFTE_MULTI_CUT,
    CSFTE_NEGATIVE_EXTEND
};

struct CSFTEEvalResult {
    CSFTEAction action = CSFTE_NONE;
    double delta_ext = 0.0;
    Value singular_beta = 0;
    int singular_depth = 0;
};

class CSFTEEngine {
public:
    CSFTEEngine() = default;

    // Evaluates whether candidate move qualifies for singular extension or fast-path bypass
    CSFTEEvalResult evaluate(const Position& pos, Move candidate_move, int depth, int ply,
                             Value beta, Value ref_score, const HARENNOutput& harenn_out,
                             Move second_best_move, int path_extensions) const;

    // Updates fractional extension accumulator and computes integer ply extension
    static void apply_fractional_extension(int base_depth, double delta_ext,
                                          double& accumulated_ext, int& out_int_ext,
                                          int& out_new_depth);

private:
    double compute_prior_singularity(const Position& pos, Move candidate_move, Move second_best_move,
                                    const HARENNOutput& harenn_out) const;
};

extern CSFTEEngine GlobalCSFTE;

} // namespace NextFish
