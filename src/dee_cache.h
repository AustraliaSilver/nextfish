#ifndef DEE_CACHE_H_INCLUDED
#define DEE_CACHE_H_INCLUDED

#include "position.h"
#include "dee.h"

namespace Stockfish {
namespace DEE {
    // Chỉ lưu kết quả cho Key hiện tại để tránh tính lại
    static thread_local Key lastKey = 0;
    static thread_local int lastTension = -1;

    inline int get_tension(const Position& pos) {
        if (pos.key() == lastKey && lastTension != -1)
            return lastTension;
        
        lastKey = pos.key();
        return lastTension = Evaluator::tension_score(pos);
    }
}
}
#endif
