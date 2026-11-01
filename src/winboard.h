#pragma once

#include "types.h"
#include "position.h"
#include "search.h"
#include <thread>
#include <atomic>
#include <string>
#include <memory>
#include <sstream>

namespace NextFish {

Move parse_move(const Position& pos, const std::string& str);

class WinBoardProtocol {
public:
    WinBoardProtocol();
    ~WinBoardProtocol();

    void run_loop();

private:
    void handle_command(const std::string& line);
    void start_thinking();
    void stop_thinking();

    Position pos;
    std::unique_ptr<StateInfo[]> history_states;
    std::unique_ptr<Move[]> history_moves;
    int history_count = 0;

    Color engine_color = BLACK;
    bool force_mode = false;
    bool post_thinking = true;

    int64_t time_remaining_ms = 300000; // 5 min default
    int64_t opponent_time_ms = 300000;
    int64_t inc_ms = 0;
    int moves_to_go = 0;

    int64_t base_time_ms = 300000;
    int64_t base_inc_ms = 0;
    int base_moves_to_go = 0;
    int fixed_depth = 64;

    std::thread search_thread;
    std::atomic<bool> is_searching{false};
};

} // namespace NextFish
