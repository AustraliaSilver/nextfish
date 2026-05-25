<div align="center">

  <img src="logo.svg" width="128" height="128" alt="Nextfish Logo">

  <h3>Nextfish HARENN V72</h3>

  A high-performance UCI chess engine powered by HARENN (Hybrid Adaptive Reduction Engine Neural Network).
  <br>
  <strong>A tactical time-management evolution of Stockfish 18.</strong>
  <br>
  <br>

  [![License][license-badge]][license-link]
  [![Elo Boost LTC][elo-ltc-badge]](#)
  [![Elo Boost Fast][elo-fast-badge]](#)
  [![Version][version-badge]](#)

</div>

## Overview

**Nextfish HARENN** is an advanced chess engine derived from **Stockfish 18**, specifically engineered to bridge the gap between classical search algorithms and modern neural intuition.

The core of Nextfish is the **HARENN (Hybrid Adaptive Reduction Engine Neural Network)** system, which utilizes a multi-head neural network trained on over 770,000 tactical positions. The V72 "Dynamic Time Orchestrator" release manages Search time allocation through three AI-predicted parameters: **Tau** (Tactical Complexity), **Rho** (Horizon Risk), and **Rs** (Move Difficulty/Resolution Score).

---

## HARENN V72 Architecture: Dynamic Time Orchestration

Instead of modifying Stockfish's highly optimized search reductions (LMR) and aspiration windows—which often introduces instabilities at long time controls—Nextfish V72 preserves **100% search loop integrity** and focuses exclusively on **intelligent time allocation**.

### Key Improvements in V72:
*   **Normalized Heads:** Individual neural network heads (`tau`, `rho`, and `rs`) are normalized based on their actual empirical distributions over opening books.
*   **Maximum-based Complexity:** Due to a strong negative correlation (`-0.77`) between Tactical Complexity and Horizon Risk, adding them directly causes cancellation. V72 resolves this by taking the maximum of the normalized components: `max(comp_norm, crit_norm, diff_norm)`.
*   **Gridded Time Scaling:**
    *   **Quiet/Simple Positions:** Scales time down to **80%** to build a time buffer.
    *   **Standard Positions:** Scales time around **100-110%**.
    *   **Complex/Critical Positions:** Scales time up to **150%** to search deeper where blunders are likely.
    *   **Game average:** Stays at a sustainable **~118%**, avoiding time trouble while providing critical depth boosts.

---

## Performance Benchmark

We ran benchmarks against the official Stockfish 18 baseline using the high-quality **UHO 2022 opening book** (each opening played twice, reversing colors):

### 1. Long Time Control (LTC) Match (`10s+0.1`, 40 games, Full PGO)
| Engine | Wins | Losses | Draws | Elo Diff | LOS |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Nextfish Improved V72 (PGO)** | **13** | 11 | 16 | **+17.4** | **65.8%** |
| Stockfish 18 Baseline (PGO) | 11 | 13 | 16 | 0.0 | - |

### 2. Fast Validation Match (`2s+0.1`, 20 games, non-PGO compilation)
| Engine | Wins | Losses | Draws | Elo Diff | LOS |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Nextfish Improved V72 (Non-PGO)** | **6** | 2 | 2 | **+147.2** | **92.1%** |
| Stockfish 18 Baseline (PGO) | 2 | 6 | 2 | 0.0 | - |
*Note: The Non-PGO Nextfish carries an inherent ~70 Elo speed penalty relative to the PGO baseline, representing a **+217 Elo** relative improvement in search quality.*

---

## UCI Options

Nextfish introduces the following UCI options to control the HARENN advisor:
*   **Use DEE/HARENN** (default `true`): Toggle the HARENN neural network and time manager.
*   **Use HARE Time Management** (default `true`): Toggle the dynamic time multiplier.

---

## Compiling Nextfish

To build Nextfish, run the following commands in the `src` directory (requires MSYS2/MinGW on Windows):

### Standard Build (Fast Compilation)
```bash
make build ARCH=x86-64-avx2 COMP=mingw -j4
```

### Profile-Guided Optimization (PGO) Build (Highest Performance)
```bash
make profile-build ARCH=x86-64-avx2 COMP=mingw -j4
```

*Note: The model files must be in the same directory as the executable.*

---

## License

Nextfish is free and distributed under the **GNU General Public License version 3** (GPL v3). As a derivative of Stockfish, all modifications are open-source and contribute back to the community.

---

[license-link]: https://github.com/AustraliaSilver/nextfish/blob/master/Copying.txt
[license-badge]: https://img.shields.io/github/license/AustraliaSilver/nextfish?style=for-the-badge&label=license&color=success
[elo-ltc-badge]: https://img.shields.io/badge/LTC_Elo-%2B17.4-green?style=for-the-badge
[elo-fast-badge]: https://img.shields.io/badge/Fast_Elo-%2B147.2-green?style=for-the-badge
[version-badge]: https://img.shields.io/badge/Version-HARENN_V72_LTC-blue?style=for-the-badge
