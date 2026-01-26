# Manual PGO build for Nextfish on Windows
$SRCS = "benchmark.cpp", "bitboard.cpp", "evaluate.cpp", "main.cpp", "misc.cpp", "movegen.cpp", "movepick.cpp", "position.cpp", "search.cpp", "thread.cpp", "timeman.cpp", "tt.cpp", "uci.cpp", "ucioption.cpp", "tune.cpp", "syzygy/tbprobe.cpp", "nnue/nnue_accumulator.cpp", "nnue/nnue_misc.cpp", "nnue/network.cpp", "nnue/features/half_ka_v2_hm.cpp", "nnue/features/full_threats.cpp", "engine.cpp", "score.cpp", "memory.cpp"
$FLAGS = "-Ofast", "-flto", "-std=c++17", "-DNDEBUG", "-DIS_64BIT", "-DUSE_PEXT", "-DUSE_POPCNT", "-DUSE_AVX2", "-DUSE_BMI2", "-mavx2", "-mbmi2", "-march=native"

Write-Host "--- Step 1: Compiling for Profile Generation ---" -ForegroundColor Cyan
g++ $FLAGS -fprofile-generate $SRCS -o nextfish_pgo_gen.exe -lpthread

Write-Host "--- Step 2: Running Benchmarks to Generate Profile ---" -ForegroundColor Cyan
.\nextfish_pgo_gen.exe bench 16 1 13 default depth
.\nextfish_pgo_gen.exe bench 16 1 13 default mixed
.\nextfish_pgo_gen.exe bench 64 1 15 default depth

Write-Host "--- Step 3: Compiling Final Optimized Binary ---" -ForegroundColor Cyan
g++ $FLAGS -fprofile-use -fprofile-correction $SRCS -o nextfish.exe -lpthread

Write-Host "--- Build Complete! ---" -ForegroundColor Green
