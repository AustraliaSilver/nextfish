#include <iostream>
#include <chrono>
#include <vector>
#include <fstream>
#include "src/types.h"
#include "src/position.h"
#include "src/harenn.h"
#include "src/movegen.h"
#include "src/smtta.h"

using namespace NextFish;

int main() {
    Bitboards::init();
    init_zobrist();
    GlobalHARENN.load_weights("Neuron Network/nextfish.harenn");
    GlobalTT.resize(64);

    Position pos_start("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    std::cout << "Starting position (White to move):\n";
    std::cout << "  Mat bal: " << pos_start.material_balance() << "\n";
    std::cout << "  HARENN:  " << GlobalHARENN.evaluate_eval_only(pos_start) << "\n";
    std::cout << "  Total:   " << (pos_start.material_balance() + GlobalHARENN.evaluate_eval_only(pos_start)) << "\n\n";

    Position pos_white_up_queen("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    std::cout << "White up a Queen (White to move):\n";
    std::cout << "  Mat bal: " << pos_white_up_queen.material_balance() << "\n";
    std::cout << "  HARENN:  " << GlobalHARENN.evaluate_eval_only(pos_white_up_queen) << "\n";
    std::cout << "  Total:   " << (pos_white_up_queen.material_balance() + GlobalHARENN.evaluate_eval_only(pos_white_up_queen)) << "\n\n";

    Position pos_black_up_queen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNB1KBNR b KQkq - 0 1");
    std::cout << "Black up a Queen (Black to move):\n";
    std::cout << "  Mat bal: " << pos_black_up_queen.material_balance() << "\n";
    std::cout << "  HARENN:  " << GlobalHARENN.evaluate_eval_only(pos_black_up_queen) << "\n";
    std::cout << "  Total:   " << (pos_black_up_queen.material_balance() + GlobalHARENN.evaluate_eval_only(pos_black_up_queen)) << "\n\n";

    return 0;
}
