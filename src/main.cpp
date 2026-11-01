#include "types.h"
#include "bitboard.h"
#include "position.h"
#include "harenn.h"
#include "winboard.h"
#include <iostream>

using namespace NextFish;

int main(int argc, char** argv) {
    // Fast I/O
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Initialize bitboards & Zobrist hashing
    Bitboards::init();
    init_zobrist();

    // Load neural network weights
    std::string net_path = "Neuron Network/nextfish.harenn";
    if (argc >= 2) {
        net_path = argv[1];
    }

    if (!GlobalHARENN.is_loaded()) {
        GlobalHARENN.load_weights(net_path);
    }

    std::cerr << "========================================\n";
    std::cerr << " NEXUS-CHESS Grand Unified Engine       \n";
    std::cerr << " Protocol: WinBoard / CECP v2           \n";
    std::cerr << " Target: ~3700 Elo                      \n";
    std::cerr << " HARENN Neural Network: " << (GlobalHARENN.is_loaded() ? "LOADED (AVX2)" : "NOT LOADED (Using Fallback HCE)") << "\n";
    std::cerr << "========================================\n";

    // Run WinBoard communication loop
    auto protocol = std::make_unique<WinBoardProtocol>();
    protocol->run_loop();

    return 0;
}
