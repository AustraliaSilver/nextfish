#include "lc0_policy.h"
#include "movegen.h"
#include "misc.h"
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
        std::string actualPath = modelPath;
        if (actualPath.empty() || actualPath == "<autodiscover>") {
            actualPath = discover_networks();
        }

        if (actualPath.empty()) return false;
        
        // Print which model is being loaded (info string to be UCI compliant)
        sync_cout << "info string Nextfish: Loading AI Model from " << actualPath << "..." << sync_endl;

        env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "Nextfish");
        Ort::SessionOptions session_options;
        
        // Cấu hình GPU T4 cực kỳ quan trọng
        OrtCUDAProviderOptions cuda_options;
        cuda_options.device_id = 0;
        cuda_options.arena_extend_strategy = 0;
        cuda_options.gpu_mem_limit = 2ULL * 1024 * 1024 * 1024; // Giới hạn 2GB cho AI
        cuda_options.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
        cuda_options.do_copy_in_default_stream = 1;
        
        session_options.AppendExecutionProvider_CUDA(cuda_options);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
        std::wstring wModelPath(actualPath.begin(), actualPath.end());
        session = std::make_unique<Ort::Session>(*env, wModelPath.c_str(), session_options);
#else
        session = std::make_unique<Ort::Session>(*env, actualPath.c_str(), session_options);
#endif
        initialized = true;
        sync_cout << "info string Nextfish: AI Model loaded on GPU (CUDA) successfully!" << sync_endl;
        return true;
    } catch (const std::exception& e) {
        std::string actualPath = modelPath;
        if (actualPath.empty() || actualPath == "<autodiscover>") actualPath = discover_networks();
        
        sync_cout << "info string Nextfish GPU Error: " << e.what() << ". Falling back to CPU..." << sync_endl;
        try {
            if (!env) env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "Nextfish");
            Ort::SessionOptions cpu_options;
            cpu_options.SetIntraOpNumThreads(2);
#ifdef _WIN32
            std::wstring wModelPath(actualPath.begin(), actualPath.end());
            session = std::make_unique<Ort::Session>(*env, wModelPath.c_str(), cpu_options);
#else
            session = std::make_unique<Ort::Session>(*env, actualPath.c_str(), cpu_options);
#endif
            initialized = true;
            return true;
        } catch (...) { return false; }
    }
}

std::string Lc0Policy::discover_networks() {
    namespace fs = std::filesystem;
    std::vector<std::string> search_paths = {
        ".",
        "./networks",
        "..",
        "../networks",
        "../../",
        "../../networks",
        "../../../",
        "../../lc0-master",
        "../../lc0-master/build",
        "../../lc0-master/networks",
        "../../../lc0-master",
        "../../../lc0-master/networks",
        "../Nextfish-dev",
        "../../Nextfish-dev"
    };

    // Also try to find CAI directory specifically if we can
    try {
        fs::path current = fs::current_path();
        int depth_limit = 10;
        while (current.has_parent_path() && depth_limit-- > 0) {
            if (current.filename() == "CAI") {
                search_paths.push_back(current.string());
                search_paths.push_back((current / "Nextfish-dev").string());
                search_paths.push_back((current / "lc0-master").string());
                search_paths.push_back((current / "lc0-master" / "networks").string());
                break;
            }
            fs::path parent = current.parent_path();
            if (parent == current) break;
            current = parent;
        }
    } catch (...) {}

    std::string best_net = "";
    fs::file_time_type best_time;
    const uintmax_t kMinFileSize = 500000; // 500 KB, giống LC0

    for (const auto& p : search_paths) {
        if (!fs::exists(p)) continue;
        try {
            for (const auto& entry : fs::directory_iterator(p)) {
                if (!entry.is_regular_file()) continue;
                if (fs::file_size(entry) < kMinFileSize) continue;
                
                std::string ext = entry.path().extension().string();
                std::string filename = entry.path().filename().string();
                
                // Prioritize .onnx for onnxruntime, then .pb.gz and .pb
                bool is_net = (ext == ".onnx" || ext == ".pb" || filename.find(".pb.gz") != std::string::npos);
                
                if (is_net) {
                    auto curr_time = fs::last_write_time(entry);
                    // If it's an .onnx, we prefer it over .pb.gz if they have similar times
                    // but for now let's just use the newest.
                    if (best_net.empty() || curr_time > best_time) {
                        best_time = curr_time;
                        best_net = entry.path().string();
                    }
                }
            }
        } catch (...) { continue; }
    }

    return best_net;
}

void Lc0Policy::encode_position(const Position& pos, float* input) {
    std::fill(input, input + 112 * 64, 0.0f);
    Color us = pos.side_to_move();

    // Planes 0-11: Piece positions (6 us, 6 them)
    // Lc0 expects board symmetry: If Black to move, flip both Rank AND File
    for (PieceType pt : {PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING}) {
        for (Color c : {WHITE, BLACK}) {
            Bitboard bb = pos.pieces(c, pt);
            int plane_idx = (pt - PAWN) + (c == us ? 0 : 6);
            while (bb) {
                Square s = pop_lsb(bb);
                // Lc0 internal representation: white is always at the bottom
                // If black to move, we flip the board
                int row = (us == WHITE) ? rank_of(s) : 7 - rank_of(s);
                int col = (us == WHITE) ? file_of(s) : 7 - file_of(s);
                input[plane_idx * 64 + row * 8 + col] = 1.0f;
            }
        }
    }

    // Planes 12-103: History (currently zeroed, as we don't have easy access to history BBs here)
    // Most Lc0 nets work okay with zeroed history, but some might prefer them.
    // For now, we keep them at 0.0f (already done by std::fill).

    // Plane 104: Side to move (1.0 for White, 0.0 for Black - but Lc0 usually expects 1.0 for "us")
    // Actually, Lc0 input format v1 (112 planes) uses:
    // 104: repetition count (0, 1, 2)
    // 105: 50-move rule
    // 106-109: castling us-OO, us-OOO, them-OO, them-OOO
    // 110: move count?
    // 111: all ones (bias)

    // Sửa lại theo chuẩn Lc0 112 planes:
    // 106-109: Castling Rights
    if (pos.can_castle(us & WHITE_OO))   std::fill(input + 106 * 64, input + 107 * 64, 1.0f);
    if (pos.can_castle(us & WHITE_OOO))  std::fill(input + 107 * 64, input + 108 * 64, 1.0f);
    if (pos.can_castle(~us & BLACK_OO))  std::fill(input + 108 * 64, input + 109 * 64, 1.0f);
    if (pos.can_castle(~us & BLACK_OOO)) std::fill(input + 109 * 64, input + 110 * 64, 1.0f);

    // 110: 50-move rule
    float rule50 = (float)pos.rule50_count() / 100.0f;
    std::fill(input + 110 * 64, input + 111 * 64, rule50);

    // 111: All ones
    std::fill(input + 111 * 64, input + 112 * 64, 1.0f);
}

Move Lc0Policy::index_to_move(int index, const Position& pos) {
    int from_sq_idx = index / 73;
    int move_type = index % 73;

    Color us = pos.side_to_move();
    
    // Lc0 model is trained on white-normalized perspective.
    // If Black to move, the input is flipped, so the model's output is also flipped.
    // A model's 'from_sq_idx' of 0 (A1) means A1 if White to move, but A8 if Black to move.
    
    int model_from_rank = from_sq_idx / 8;
    int model_from_file = from_sq_idx % 8;
    
    int actual_from_rank = (us == WHITE) ? model_from_rank : (7 - model_from_rank);
    int actual_from_file = (us == WHITE) ? model_from_file : (7 - model_from_file);
    
    Square from_sq = make_square(File(actual_from_file), Rank(actual_from_rank));

    Square to_sq = SQ_NONE;
    PieceType prom = NO_PIECE_TYPE;

    // Lc0 directions (from side-to-move perspective): 
    // 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW
    static const int dr[] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int df[] = {0, 1, 1, 1, 0, -1, -1, -1};

    if (move_type < 56) { // Queen-like moves
        int direction = move_type / 7;
        int distance = (move_type % 7) + 1;
        
        int model_to_rank = model_from_rank + dr[direction] * distance;
        int model_to_file = model_from_file + df[direction] * distance;

        if (model_to_rank >= 0 && model_to_rank <= 7 && model_to_file >= 0 && model_to_file <= 7) {
            int actual_to_rank = (us == WHITE) ? model_to_rank : (7 - model_to_rank);
            int actual_to_file = (us == WHITE) ? model_to_file : (7 - model_to_file);
            to_sq = make_square(File(actual_to_file), Rank(actual_to_rank));
        }
    } else if (move_type < 64) { // Knight moves
        static const int knr[] = {2, 1, -1, -2, -2, -1, 1, 2};
        static const int knf[] = {1, 2, 2, 1, -1, -2, -2, -1};
        
        int model_to_rank = model_from_rank + knr[move_type-56];
        int model_to_file = model_from_file + knf[move_type-56];
        
        if (model_to_rank >= 0 && model_to_rank <= 7 && model_to_file >= 0 && model_to_file <= 7) {
            int actual_to_rank = (us == WHITE) ? model_to_rank : (7 - model_to_rank);
            int actual_to_file = (us == WHITE) ? model_to_file : (7 - model_to_file);
            to_sq = make_square(File(actual_to_file), Rank(actual_to_rank));
        }
    } else { // Underpromotions
        int p_type = (move_type - 64) / 3; // 0=Knight, 1=Bishop, 2=Rook
        int p_dir = (move_type - 64) % 3;  // 0=Left diagonal, 1=Forward, 2=Right diagonal
        
        int model_to_rank = model_from_rank + 1; // Promotions are always 1 step forward in model space
        int model_to_file = model_from_file + (p_dir - 1);
        
        if (model_to_rank == 7 && model_to_file >= 0 && model_to_file <= 7) {
            int actual_to_rank = (us == WHITE) ? 7 : 0;
            int actual_to_file = (us == WHITE) ? model_to_file : (7 - model_to_file);
            to_sq = make_square(File(actual_to_file), Rank(actual_to_rank));
            prom = (p_type == 0) ? KNIGHT : (p_type == 1) ? BISHOP : ROOK;
        }
    }

    if (to_sq == SQ_NONE) return Move::none();

    // Queen promotion
    if (prom == NO_PIECE_TYPE && pos.piece_on(from_sq) == make_piece(us, PAWN)) {
        if (rank_of(to_sq) == (us == WHITE ? RANK_8 : RANK_1))
            prom = QUEEN;
    }

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
