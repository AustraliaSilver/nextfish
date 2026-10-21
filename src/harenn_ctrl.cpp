/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "harenn_ctrl.h"
#include "harenn.h"
#include "bitboard.h"
#include "position.h"
#include "ucioption.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <vector>

namespace Stockfish {

namespace HARENN {

namespace {
    // Tunable parameters, refreshed from UCI options at start of search.
    // They are atomic because the helper threads read them concurrently
    // while the main thread publishes a refreshed set.
    std::atomic<float> tm_center{0.35f};
    std::atomic<float> tm_slope{14.3f};
    std::atomic<float> tm_range_min{95.0f};
    std::atomic<float> tm_range_max{105.0f};
    std::atomic<float> ext_threshold_white{0.8228f};
    std::atomic<float> ext_threshold_black{0.7060f};
}

void Controller::init(const std::string& binaryDirectory) {
    GuidanceProvider::init(binaryDirectory);
}

void Controller::refresh_params(const OptionsMap& options) {
    tm_center.store(options["HARE TM Center"] * 0.01f, std::memory_order_relaxed);
    tm_slope.store(options["HARE TM Slope"] * 0.1f, std::memory_order_relaxed);
    tm_range_min.store((float) (int) options["HARE TM Range Min"], std::memory_order_relaxed);
    tm_range_max.store((float) (int) options["HARE TM Range Max"], std::memory_order_relaxed);
    ext_threshold_white.store(options["HARE Ext Threshold White"] * 0.001f, std::memory_order_relaxed);
    ext_threshold_black.store(options["HARE Ext Threshold Black"] * 0.001f, std::memory_order_relaxed);
}

EvalResult Controller::get_analysis(const Position& pos, NumaReplicatedAccessToken numaToken) {
    return GuidanceProvider::query(pos, numaToken);
}

int Controller::get_search_extension(const EvalResult& res, Color sideToMove) {
    // The model reports two properties of the position:
    //   rho - horizon risk, how likely it is that the position is still
    //         unresolved further down the line,
    //   rs  - resolution score, how settled the position already is.
    // The position deserves more search when the horizon risk is high and the
    // resolution score is still low. Either signal on its own is only weak
    // evidence, but the horizon risk of the current model never reaches the
    // threshold, so requiring both conditions would disable the extension
    // completely. The condition is therefore a disjunction.
    const float threshold =
      sideToMove == BLACK ? ext_threshold_black.load(std::memory_order_relaxed)
                          : ext_threshold_white.load(std::memory_order_relaxed);

    if (res.rho > threshold || res.rs < (1.0f - threshold)) return 1;

    return 0;
}

int Controller::get_time_multiplier(const Position& pos) {
    if (!GuidanceProvider::is_model_loaded()) return 100;

    EvalResult res = get_analysis(pos, NumaReplicatedAccessToken(0));

    float mult = 100.0f + (res.tau - tm_center.load(std::memory_order_relaxed))
                           * tm_slope.load(std::memory_order_relaxed);
    return int(std::clamp(mult, tm_range_min.load(std::memory_order_relaxed),
                          tm_range_max.load(std::memory_order_relaxed)));
}

} // namespace HARENN

} // namespace Stockfish
