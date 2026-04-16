<div align="center">

  <img src="logo.svg" width="128" height="128" alt="Nextfish Logo">

  <h3>Nextfish HARENN</h3>

  A high-performance UCI chess engine powered by HARENN (Hybrid Adaptive Reduction Engine Neural Network).
  <br>
  <strong>A tactical evolution of Stockfish 18.</strong>
  <br>
  <br>

  [![License][license-badge]][license-link]
  [![Elo Boost][elo-badge]](#)
  [![Version][version-badge]](#)

</div>

## Overview

**Nextfish HARENN** is an advanced chess engine derived from **Stockfish 18**, specifically engineered to bridge the gap between classical search algorithms and modern neural intuition. 

The core of Nextfish is the **HARENN (Hybrid Adaptive Reduction Engine Neural Network)** system, which utilizes a specialized 0.38MB neural network to analyze tactical complexity in real-time. This allowing the engine to make smarter decisions about search depth and move ordering, resulting in a **+21 Elo increase** over the Stockfish 18 baseline.

## Key Features

*   **HARENN Core:** A lightweight, AVX2-optimized neural network that predicts tactical volatility (Tau) and evaluation hints.
*   **Dynamic Exchange Evaluation (DEE):** An enhanced move-ordering heuristic that resolves neutral exchanges by calculating numerical attacker/defender imbalances.
*   **Tactical Move Ordering:** Real-time identification of "hanging pieces" and king-ring pressure to prioritize critical tactical responses.
*   **Optimized for Windows:** Fully compatible with MinGW/GCC with optimized AVX2 and SSE4.1 builds.

## Performance (Nextfish-V7 vs Baseline)

| Engine | Wins | Losses | Draws | Elo |
| :--- | :---: | :---: | :---: | :---: |
| **Nextfish HARENN** | **31** | 26 | 23 | **+21.7** |
| Stockfish 18 Baseline | 26 | 31 | 23 | 0.0 |

*Tested at 1s+0.01s time control over 80 games.*

## Compiling Nextfish

### Windows (MinGW-w64)
To build the most optimized version for your CPU:

```bash
cd src
make build ARCH=x86-64-avx2 COMP=mingw
```

### Linux / WSL
```bash
cd src
make build ARCH=x86-64-avx2
```

The model file `nextfish.harenn` must be located in the same directory as the executable.

## Terms of Use

Nextfish is free and distributed under the **GNU General Public License version 3** (GPL v3). As a derivative of Stockfish, all modifications are open-source and contribute back to the community's knowledge of tactical search optimization.

---

[license-link]: https://github.com/AustraliaSilver/nextfish/blob/master/Copying.txt
[license-badge]: https://img.shields.io/github/license/AustraliaSilver/nextfish?style=for-the-badge&label=license&color=success
[elo-badge]: https://img.shields.io/badge/Elo-Boost_%2B21.7-blue?style=for-the-badge
[version-badge]: https://img.shields.io/badge/Version-HARENN_V7_Release-orange?style=for-the-badge
