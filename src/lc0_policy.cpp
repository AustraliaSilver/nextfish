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
std::vector<const char*> Lc0Policy::input_node_names = {"input:0"};
std::vector<const char*> Lc0Policy::output_node_names = {"policy_output/Softmax:0", "value_output/Tanh:0"};

bool Lc0Policy::initialize(const std::string& modelPath) {
    try {
        if (modelPath.empty()) return false;
        env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "Nextfish");
        Ort::SessionOptions session_options;
        
        // Kích hoạt CUDA cho Kaggle GPU T4
        try {
            OrtCUDAProviderOptions cuda_options;
            cuda_options.device_id = 0;
            session_options.AppendExecutionProvider_CUDA(cuda_options);
        } catch (...) {}

        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
        std::wstring wModelPath(modelPath.begin(), modelPath.end());
        session = std::make_unique<Ort::Session>(*env, wModelPath.c_str(), session_options);
#else
        session = std::make_unique<Ort::Session>(*env, modelPath.c_str(), session_options);
#endif
        initialized = true;
        std::cout << "Nextfish: AI Model (112 planes) loaded on GPU successfully!" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Nextfish Error: " << e.what() << std::endl;
        return false;
    }
}

void Lc0Policy::encode_position(const Position& pos, float* input) {
    std::fill(input, input + 112 * 64, 0.0f);
    Color us = pos.side_to_move();

    // Planes 0-11: Piece positions (6 us, 6 them)
    for (PieceType pt : {PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING}) {
        for (Color c : {WHITE, BLACK}) {
            Bitboard bb = pos.pieces(c, pt);
            int plane_idx = (pt - PAWN) + (c == us ? 0 : 6);
            while (bb) {
                Square s = pop_lsb(bb);
                int row = (us == WHITE) ? rank_of(s) : 7 - rank_of(s);
                int col = file_of(s); // Lc0 không flip file theo mặc định
                input[plane_idx * 64 + row * 8 + col] = 1.0f;
            }
        }
    }

    // Plane 104: Side to move (1.0 for White, but here it's perspective-neutral)
    // Lc0 thường dùng plane này để chỉ định lượt đi nếu không flip board.
    
    // Planes 106-109: Castling Rights
    if (pos.can_castle(us & WHITE_OO))   std::fill(input + 106 * 64, input + 107 * 64, 1.0f);
    if (pos.can_castle(us & WHITE_OOO))  std::fill(input + 107 * 64, input + 108 * 64, 1.0f);
    if (pos.can_castle(~us & BLACK_OO))  std::fill(input + 108 * 64, input + 109 * 64, 1.0f);
    if (pos.can_castle(~us & BLACK_OOO)) std::fill(input + 109 * 64, input + 110 * 64, 1.0f);

    // Plane 110: Rule 50 (normalized)
    float rule50 = (float)pos.rule50_count() / 100.0f;
    std::fill(input + 110 * 64, input + 111 * 64, rule50);
}

Move Lc0Policy::index_to_move(int index, const Position& pos) {
    int from_sq_idx = index / 73;
    int move_type = index % 73;

    Color us = pos.side_to_move();
    int sf_from_idx = (us == WHITE) ? from_sq_idx : (from_sq_idx ^ 56);
    Square from_sq = Square(sf_from_idx);
    int from_rank = rank_of(from_sq);
    int from_file = file_of(from_sq);

    Square to_sq = SQ_NONE;
    PieceType prom = NO_PIECE_TYPE;

    static const int dr[] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int df[] = {0, 1, 1, 1, 0, -1, -1, -1};

    if (move_type < 56) {
        int direction = move_type / 7;
        int distance = (move_type % 7) + 1;
        int to_rank = from_rank + (us == WHITE ? dr[direction] : -dr[direction]) * distance;
        int to_file = from_file + df[direction] * distance; // KHÔNG flip file

        if (to_rank >= 0 && to_rank <= 7 && to_file >= 0 && to_file <= 7) {
            to_sq = make_square(File(to_file), Rank(to_rank));
            if (pos.piece_on(from_sq) == make_piece(us, PAWN) && rank_of(to_sq) == (us == WHITE ? RANK_8 : RANK_1))
                prom = QUEEN;
        }
    } else if (move_type < 64) {
        static const int knr[] = {2, 1, -1, -2, -2, -1, 1, 2};
        static const int knf[] = {1, 2, 2, 1, -1, -2, -2, -1};
        int to_rank = from_rank + (us == WHITE ? knr[move_type-56] : -knr[move_type-56]);
        int to_file = from_file + knf[move_type-56];
        if (to_rank >= 0 && to_rank <= 7 && to_file >= 0 && to_file <= 7)
            to_sq = make_square(File(to_file), Rank(to_rank));
    } else {
        int p_type = (move_type - 64) / 3;
        int p_dir = (move_type - 64) % 3;
        int to_rank = (us == WHITE) ? RANK_8 : RANK_1;
        int to_file = from_file + (p_dir - 1);
        if (to_file >= 0 && to_file <= 7) {
            to_sq = make_square(File(to_file), Rank(to_rank));
            prom = (p_type == 0) ? KNIGHT : (p_type == 1) ? BISHOP : ROOK;
        }
    }

    if (to_sq == SQ_NONE) return Move::none();
    for (const auto& m : MoveList<LEGAL>(pos)) {
        if (m.from_sq() == from_sq && m.to_sq() == to_sq && m.promotion_type() == prom)
            return m;
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
        std::vector<std::pair<float, int>> probs;
        for (int i = 0; i < 1858; ++i) probs.push_back({policy_data[i], i});
        std::sort(probs.rbegin(), probs.rend());

        std::vector<Move> topMoves;
        for (int i = 0; i < (int)probs.size() && (int)topMoves.size() < n; ++i) {
            Move m = index_to_move(probs[i].second, pos);
            if (m != Move::none() && std::find(topMoves.begin(), topMoves.end(), m) == topMoves.end())
                topMoves.push_back(m);
        }
        return topMoves;
    } catch (...) { return {}; }
}

} // namespace Nextfish