#ifndef NEXTFISH_TIMEMAN_H_INCLUDED
#define NEXTFISH_TIMEMAN_H_INCLUDED

#include "misc.h"
#include "types.h"

namespace Stockfish {
    namespace Search {
        struct LimitsType;
    }
}

namespace Nextfish {

class TimeManager {
public:
    void init(const Stockfish::Search::LimitsType& limits, 
              Stockfish::Color us, 
              int ply);

    Stockfish::TimePoint optimum() const;
    Stockfish::TimePoint maximum() const;
    
    // Dynamic Pulse: Điều chỉnh thời gian dựa trên biến động của ván đấu
    void update_pulse(int bestMoveChanges, int scoreDiff);

    // Compatibility functions
    template<typename FUNC>
    Stockfish::TimePoint elapsed(FUNC nodes [[maybe_unused]]) const {
        return elapsed_time(); // Simplified: always use wall time for now
    }
    Stockfish::TimePoint elapsed_time() const;
    void clear();
    void advance_nodes_time(std::int64_t nodes);

private:
    Stockfish::TimePoint startTime;
    Stockfish::TimePoint baseOptimum;
    Stockfish::TimePoint baseMaximum;
    Stockfish::TimePoint currentOptimum;
    Stockfish::TimePoint currentMaximum;
    
    double pulseFactor; // Hệ số nhân thời gian động
};

}

#endif // NEXTFISH_TIMEMAN_H_INCLUDED
