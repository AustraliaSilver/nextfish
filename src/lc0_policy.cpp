#include "lc0_policy.h"
#include "uci.h"
#include <iostream>
#include <algorithm>

namespace Nextfish {

bool Lc0Policy::initialized = false;
std::string Lc0Policy::currentModelPath = "";

bool Lc0Policy::initialize(const std::string& modelPath) {
    // Trong thực tế, bạn sẽ khởi tạo ONNX Runtime Session ở đây
    // Ort::Session session(env, modelPath.c_str(), session_options);
    
    currentModelPath = modelPath;
    initialized = true;
    return true;
}

std::vector<Move> Lc0Policy::get_top_moves(const Position& pos, int n) {
    std::vector<Move> topMoves;
    if (!initialized) return topMoves;

    // GIẢ LẬP: Trong thực tế, đoạn này sẽ:
    // 1. Chuyển đổi Position pos sang Tensor input (định dạng Lc0)
    // 2. Chạy session.Run() để lấy Policy Output
    // 3. Giải mã Output thành danh sách Move và xác suất
    // 4. Sắp xếp và lấy n nước đi đầu tiên
    
    // Lưu ý: Để code này chạy được, bạn cần cài đặt ONNX Runtime 
    // và viết logic chuyển đổi Board -> Tensor (khá phức tạp).
    
    return topMoves; 
}

} // namespace Nextfish
