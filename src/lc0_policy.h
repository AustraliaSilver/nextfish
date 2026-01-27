#ifndef LC0_POLICY_H
#define LC0_POLICY_H

#include "types.h"
#include "position.h"
#include <vector>
#include <string>

namespace Nextfish {

struct PolicyMove {
    Move move;
    float probability;
};

class Lc0Policy {
public:
    static bool initialize(const std::string& modelPath);
    static std::vector<Move> get_top_moves(const Position& pos, int n = 7);
    static bool is_ready() { return initialized; }

private:
    static bool initialized;
    static std::string currentModelPath;
    // Thư viện ONNX Runtime sẽ được khởi tạo tại đây trong file .cpp
};

} // namespace Nextfish

#endif
