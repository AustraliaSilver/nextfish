#ifndef LC0_POLICY_H
#define LC0_POLICY_H

#include "types.h"
#include "position.h"
#include <vector>
#include <string>
#include <memory>
#include <onnxruntime_cxx_api.h>

namespace Nextfish {

using namespace Stockfish;

class Lc0Policy {
public:
    static bool initialize(const std::string& modelPath);
    static std::vector<Move> get_top_moves(const Position& pos, int n = 7);
    static bool is_ready() { return initialized; }
    static void set_active(bool active) { isActive = active; }
    static bool is_active() { return isActive; }

private:
    static bool initialized;
    static bool isActive;
    static std::unique_ptr<Ort::Env> env;
    static std::unique_ptr<Ort::Session> session;
    static std::vector<const char*> input_node_names;
    static std::vector<const char*> output_node_names;

    static void encode_position(const Position& pos, float* input);
    static Move index_to_move(int index, const Position& pos);
};

} // namespace Nextfish

#endif
