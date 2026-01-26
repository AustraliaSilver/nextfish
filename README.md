# 👑 Nextfish Overlord v1.0

Nextfish is a high-performance chess engine forked from Stockfish 17.1, specifically optimized for **AMD Ryzen 7 5700U (AVX2)** and similar architectures.

## 🚀 Key Improvements

- **Aggressive NNUE Thresholding**: Set to **900** (compared to SF 962) to engage the large network earlier for deeper strategic insight in complex positions.
- **Asymmetric Time Management**: Black receives a **+15% time bonus** to bolster defensive stability and find hidden draws in TCEC-style endgames.
- **Ultimatum Optimism**: Enhanced White optimism scaling (**112%**) to push for decisive advantages in middle-game transitions.
- **Ryzen 5700U Tuning**: Optimized for Zen 2 architecture cache-line alignment (64-byte TT cluster padding).

## 📊 Automated Elo Testing

This repository features a **Continuous Integration (CI)** system. Every time code is pushed, GitHub Actions:
1. Builds the latest Nextfish source.
2. Pulls the official Stockfish 17.1 baseline.
3. Runs a **100-game match** (10s + 0.1s) using `fastchess`.
4. Outputs the Elo difference directly in the Action logs.

## 🛠️ How to Build

### Windows (MSYS2/MinGW)
```bash
cd src
make -j profile-build ARCH=x86-64-avx2
```

### Linux
```bash
cd src
make -j build ARCH=x86-64-avx2
```

## 📜 License
Nextfish is licensed under the **GNU General Public License v3.0**.