#include "winboard.h"
#include "pawn_table.h"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace NextFish {

WinBoardProtocol::WinBoardProtocol() {
    history_states = std::make_unique<StateInfo[]>(1024);
    history_moves = std::make_unique<Move[]>(1024);
    pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    history_count = 0;
}

WinBoardProtocol::~WinBoardProtocol() {
    stop_thinking();
}

Move parse_move(const Position& pos, const std::string& str) {
    if (str.empty()) return MOVE_NONE;

    // Handle SAN castling if inputted (e.g. from human player or WinBoard GUI)
    if (str == "O-O" || str == "o-o" || str == "0-0") {
        std::string king_move = (pos.side_to_move() == WHITE) ? "e1g1" : "e8g8";
        return parse_move(pos, king_move);
    }
    if (str == "O-O-O" || str == "o-o-o" || str == "0-0-0") {
        std::string king_move = (pos.side_to_move() == WHITE) ? "e1c1" : "e8c8";
        return parse_move(pos, king_move);
    }

    if (str.length() < 4) return MOVE_NONE;

    File from_f = static_cast<File>(str[0] - 'a');
    Rank from_r = static_cast<Rank>(str[1] - '1');
    File to_f = static_cast<File>(str[2] - 'a');
    Rank to_r = static_cast<Rank>(str[3] - '1');

    if (from_f < 0 || from_f > 7 || from_r < 0 || from_r > 7) return MOVE_NONE;
    if (to_f < 0 || to_f > 7 || to_r < 0 || to_r > 7) return MOVE_NONE;

    Square from = make_square(from_f, from_r);
    Square to = make_square(to_f, to_r);

    PieceType promo = NO_PIECE_TYPE;
    if (str.length() >= 5) {
        char p = std::tolower(str[4]);
        switch (p) {
            case 'n': promo = KNIGHT; break;
            case 'b': promo = BISHOP; break;
            case 'r': promo = ROOK; break;
            case 'q': promo = QUEEN; break;
        }
    }

    MoveList legal;
    generate_all_legal(pos, legal);

    for (int i = 0; i < legal.size(); ++i) {
        Move m = legal[i];
        if (m.from() == from && m.to() == to) {
            if (m.type() == PROMOTION) {
                // If promotion piece omitted in 4-character move string, default to QUEEN
                if (promo == NO_PIECE_TYPE && m.promo_type() == QUEEN) return m;
                if (m.promo_type() == promo) return m;
            } else {
                return m;
            }
        }
    }

    return MOVE_NONE;
}

void WinBoardProtocol::stop_thinking() {
    GlobalSearch.stop();
    if (search_thread.joinable()) {
        search_thread.join();
    }
    is_searching.store(false);
}

void WinBoardProtocol::start_thinking() {
    stop_thinking();

    SearchLimits limits;
    limits.time = time_remaining_ms;
    limits.inc = inc_ms;
    limits.opponent_time = opponent_time_ms;
    limits.moves_to_go = moves_to_go;
    limits.depth = fixed_depth;

    is_searching.store(true);

    search_thread = std::thread([this, limits]() {
        GlobalSearch.start_search(
            pos, limits,
            [this](int depth, Value score, int64_t time_ms, uint64_t nodes, const std::string& pv) {
                if (post_thinking) {
                    // WinBoard thinking format: depth score time_in_centisec nodes pv
                    int64_t centisec = time_ms / 10;
                    std::cout << depth << " " << score << " " << centisec << " " << nodes << " " << pv << "\n";
                    std::cout.flush();
                }
            },
            [this](Move best_move) {
                if (!best_move.is_valid() || !pos.is_legal(best_move)) {
                    MoveList legal;
                    generate_all_legal(pos, legal);
                    if (!legal.empty()) {
                        best_move = legal[0];
                    } else {
                        best_move = MOVE_NONE;
                    }
                }
                if (best_move.is_valid()) {
                    if (history_count < 1020) {
                        history_moves[history_count] = best_move;
                        pos.make_move(best_move, history_states[history_count++]);
                    }
                    std::string m_str = move_to_string(best_move);
                    std::cout << "move " << m_str << "\n";
                    std::cout.flush();
                } else {
                    // No legal move
                    if (pos.is_in_check()) {
                        std::cout << (pos.side_to_move() == WHITE ? "0-1 {Black mates}\n" : "1-0 {White mates}\n");
                    } else {
                        std::cout << "1/2-1/2 {Stalemate}\n";
                    }
                    std::cout.flush();
                }
                is_searching.store(false);
            }
        );
    });
}

void WinBoardProtocol::handle_command(const std::string& line) {
    if (line.empty()) return;

    std::istringstream ss(line);
    std::string cmd;
    if (!(ss >> cmd) || cmd.empty()) return;

    if (cmd == "xboard") {
        // Handshake
    } else if (cmd == "protover") {
        int ver = 2;
        ss >> ver;
        std::cout << "feature ping=1 setboard=1 usermove=1 time=1 memory=1 smp=1 sigint=0 sigterm=0 reuse=1 variants=\"normal\" myname=\"NEXUS-CHESS\" done=1\n";
        std::cout.flush();
    } else if (cmd == "accepted" || cmd == "rejected") {
        // Acknowledged
    } else if (cmd == "new") {
        stop_thinking();
        pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        history_count = 0;
        engine_color = BLACK;
        force_mode = false;
        fixed_depth = 64;
        time_remaining_ms = base_time_ms;
        opponent_time_ms = base_time_ms;
        inc_ms = base_inc_ms;
        moves_to_go = base_moves_to_go;
        GlobalHAMO.clear();
        GlobalTT.clear();
        GlobalHARENN.clear_cache();
        GlobalPawnTable.clear();
    } else if (cmd == "force") {
        stop_thinking();
        force_mode = true;
    } else if (cmd == "go") {
        stop_thinking();
        force_mode = false;
        engine_color = pos.side_to_move();
        start_thinking();
    } else if (cmd == "playother") {
        stop_thinking();
        force_mode = false;
        engine_color = ~pos.side_to_move();
    } else if (cmd == "usermove") {
        stop_thinking();
        std::string move_str;
        ss >> move_str;
        Move m = parse_move(pos, move_str);
        if (m.is_valid()) {
            if (history_count < 1020) {
                history_moves[history_count] = m;
                pos.make_move(m, history_states[history_count++]);
            }
            if (!force_mode && pos.side_to_move() == engine_color) {
                start_thinking();
            }
        } else {
            std::cout << "Error (unknown command): " << line << "\n";
            std::cout.flush();
        }
    } else if (cmd == "time") {
        int64_t centisec = 0;
        ss >> centisec;
        time_remaining_ms = centisec * 10;
    } else if (cmd == "otim") {
        int64_t centisec = 0;
        ss >> centisec;
        opponent_time_ms = centisec * 10;
    } else if (cmd == "level") {
        int mps = 0;
        std::string base_str;
        double inc_sec = 0.0;
        ss >> mps >> base_str >> inc_sec;
        moves_to_go = mps;
        base_moves_to_go = mps;
        inc_ms = static_cast<int64_t>(inc_sec * 1000.0);
        base_inc_ms = inc_ms;
        auto colon = base_str.find(':');
        if (colon != std::string::npos) {
            double min = std::atof(base_str.substr(0, colon).c_str());
            double sec = std::atof(base_str.substr(colon + 1).c_str());
            time_remaining_ms = static_cast<int64_t>((min * 60.0 + sec) * 1000.0);
        } else {
            double min = std::atof(base_str.c_str());
            time_remaining_ms = static_cast<int64_t>(min * 60.0 * 1000.0);
        }
        base_time_ms = time_remaining_ms;
        opponent_time_ms = time_remaining_ms;
    } else if (cmd == "st") {
        double sec = 0.0;
        ss >> sec;
        time_remaining_ms = static_cast<int64_t>(sec * 1000.0);
        inc_ms = 0;
        moves_to_go = 1;
        fixed_depth = 64;
    } else if (cmd == "sd") {
        int d = 64;
        ss >> d;
        fixed_depth = d;
    } else if (cmd == "memory") {
        stop_thinking();
        size_t mb = 64;
        ss >> mb;
        GlobalTT.resize(mb);
    } else if (cmd == "cores") {
        int c = 1;
        ss >> c;
        // Configured thread count
    } else if (cmd == "variant") {
        // Acknowledge standard chess variant
    } else if (cmd == "easy" || cmd == "hard" || cmd == "random" || cmd == "rating" || cmd == "name" || cmd == "computer") {
        // Ignored Winboard commands
    } else if (cmd == "ping") {
        int n = 0;
        ss >> n;
        std::cout << "pong " << n << "\n";
        std::cout.flush();
    } else if (cmd == "post") {
        post_thinking = true;
    } else if (cmd == "nopost") {
        post_thinking = false;
    } else if (cmd == "setboard") {
        stop_thinking();
        std::string fen_rest;
        std::getline(ss, fen_rest);
        if (!fen_rest.empty() && fen_rest[0] == ' ') {
            fen_rest = fen_rest.substr(1);
        }
        pos.set(fen_rest);
        history_count = 0;
    } else if (cmd == "undo") {
        stop_thinking();
        if (history_count > 0) {
            history_count--;
            pos.unmake_move(history_moves[history_count]);
        }
    } else if (cmd == "?") {
        // Move immediately
        if (is_searching.load()) {
            GlobalSearch.stop();
        }
    } else if (cmd == "result") {
        stop_thinking();
    } else if (cmd == "quit") {
        stop_thinking();
        std::exit(0);
    } else {
        stop_thinking();
        Move m = parse_move(pos, cmd);
        if (m.is_valid()) {
            if (history_count < 1020) {
                history_moves[history_count] = m;
                pos.make_move(m, history_states[history_count++]);
            }
            if (!force_mode && pos.side_to_move() == engine_color) {
                start_thinking();
            }
        }
    }
}

void WinBoardProtocol::run_loop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        handle_command(line);
    }
    if (search_thread.joinable()) {
        search_thread.join();
    }
    is_searching.store(false);
}

} // namespace NextFish
