#include "lc0_policy.h"
#include "movegen.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace Nextfish {

using namespace Stockfish;

bool Lc0Policy::initialized = false;
bool Lc0Policy::isActive = true;
std::unique_ptr<Ort::Env> Lc0Policy::env = nullptr;
std::unique_ptr<Ort::Session> Lc0Policy::session = nullptr;
std::vector<const char*> Lc0Policy::input_node_names = {"input"};
std::vector<const char*> Lc0Policy::output_node_names = {"policy", "value"};

bool Lc0Policy::initialize(const std::string& modelPath) {
    try {
        if (modelPath.empty()) return false;
        
        env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "Nextfish");
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Nạp Model
#ifdef _WIN32
        std::wstring wModelPath(modelPath.begin(), modelPath.end());
        session = std::make_unique<Ort::Session>(*env, wModelPath.c_str(), session_options);
#else
        session = std::make_unique<Ort::Session>(*env, modelPath.c_str(), session_options);
#endif
        
        initialized = true;
        std::cout << "Nextfish: Lc0 Model AI loaded successfully from " << modelPath << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Nextfish Error: Failed to load AI Model: " << e.what() << std::endl;
        return false;
    }
}

void Lc0Policy::encode_position(const Position& pos, float* input) {
    // Lc0 Input format: 112 planes x 8x8 (Tối giản cho phiên bản hiện tại)
    std::fill(input, input + 112 * 64, 0.0f);
    Color us = pos.side_to_move();

    for (PieceType pt : {PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING}) {
        for (Color c : {WHITE, BLACK}) {
            Bitboard bb = pos.pieces(c, pt);
            int plane_idx = (pt - PAWN) + (c == us ? 0 : 6);
            while (bb) {
                Square s = pop_lsb(bb);
                // Lc0 luôn nhìn từ góc độ người chơi hiện tại
                int row = (us == WHITE) ? rank_of(s) : 7 - rank_of(s);
                int col = (us == WHITE) ? file_of(s) : 7 - file_of(s);
                input[plane_idx * 64 + row * 8 + col] = 1.0f;
            }
        }
    }
}

Move Lc0Policy::index_to_move(int index, const Position& pos) {
    // Mapping Lc0 Policy Index (1858) to Stockfish Move
    int from_idx = index / 73;
    int move_type = index % 73;

    int from_sq_idx = (pos.side_to_move() == WHITE) ? from_idx : (from_idx ^ 56);
    Square from_sq = Square(from_sq_idx);
    
    // Tìm nước đi trong danh sách hợp lệ khớp với logic Lc0
    for (const auto& m : MoveList<LEGAL>(pos)) {
        if (m.from_sq() == from_sq) {
            // Logic đơn giản: Nếu Square đích khớp, trả về (Cần cải thiện logic chi tiết cho Queen/Knight/Promo)
            // Trong bản này, chúng ta sẽ so sánh m dựa trên move_type nếu cần chính xác tuyệt đối
            // Tạm thời: Trả về nước đi đầu tiên khớp Square đi.
            return m; 
        }
    }
    return Move::none();
}

std::vector<Move> Lc0Policy::get_top_moves(const Position& pos, int n) {
    if (!initialized || !isActive) return {};

    try {
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<float> input_tensor_values(112 * 64);
        encode_position(pos, input_tensor_values.data());

        std::vector<int64_t> input_shape = {1, 112, 8, 8};
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, input_tensor_values.data(), input_tensor_values.size(), 
            input_shape.data(), input_shape.size());

        auto output_tensors = session->Run(Ort::RunOptions{nullptr}, 
                                           input_node_names.data(), &input_tensor, 1, 
                                           output_node_names.data(), output_node_names.size());

        float* policy_data = output_tensors[0].GetTensorMutableData<float>();
        
        // Sắp xếp các chỉ số Policy theo xác suất giảm dần
        std::vector<std::pair<float, int>> probs;
        for (int i = 0; i < 1858; ++i) {
            probs.push_back({policy_data[i], i});
        }
        std::sort(probs.rbegin(), probs.rend());

        std::vector<Move> topMoves;
        for (int i = 0; i < (int)probs.size() && (int)topMoves.size() < n; ++i) {
            Move m = index_to_move(probs[i].second, pos);
            if (m != Move::none()) {
                // Kiểm tra trùng lặp
                if (std::find(topMoves.begin(), topMoves.end(), m) == topMoves.end())
                    topMoves.push_back(m);
            }
        }
        return topMoves;

    } catch (...) {
        return {};
    }
}

} // namespace Nextfish