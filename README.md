# Nextfish 🐟

**Nextfish** is a sophisticated derivative of the world-champion chess engine **Stockfish**. It builds upon the powerful framework of Stockfish 16.1 (dev) to introduce experimental search heuristics, dynamic time management, and non-linear evaluation strategies.

The goal of Nextfish is not just to increase Elo, but to explore **"Human-like Aggression"** and **"Dynamic Resilience"** in computer chess.

![Build Status](https://github.com/AustraliaSilver/nextfish/actions/workflows/test_elo.yml/badge.svg)
![License](https://img.shields.io/badge/License-GPL%20v3-blue.svg)

---

## 🚀 Key Features

### 1. Singularity Reborn (v63) Strategy
Nextfish v63 introduces a **Non-linear Logic** approach:
*   **Conditional Optimism:** The engine plays aggressively (+25cp) when the King is safe, but instantly reverts to cautious mode when under threat.
*   **Dynamic Time Management:** Uses a "Panic Factor". If the evaluation drops significantly (>20cp), Nextfish spends **2.0x** more time than usual to find a defense.
*   **Anti-Horizon Pruning:** Aggressively prunes "drawish" lines to focus computational power on complex, winning variations.

### 2. Pulsar & Plasma Core
Inherited from v58-v62, the core logic uses a "Volatility Threshold":
*   **Radar System:** Detects tactical volatility. If a move causes a sharp score drop, the engine triggers **Code Red**, reducing pruning to ensure maximum calculation depth.
*   **SPSA Tuned:** Key parameters (Optimism, Pessimism, LMR) have been fine-tuned using Simultaneous Perturbation Stochastic Approximation (SPSA) over thousands of games.

### 3. Fine-Grained Precision
Unlike standard engines that use integer parameters, Nextfish utilizes **double-precision floating-point** values for its internal heuristics, allowing for micro-adjustments (e.g., Optimism = 20.85 instead of 21).

---

## 🏆 Performance

Nextfish is automatically tested against **Stockfish 17.1** via GitHub Actions on every commit.
*   **Baseline:** Stockfish 16.1 (Dev)
*   **Target:** Competing with Stockfish 17+ using superior time management and aggression.

---

## 🛠️ Compilation & Usage

Nextfish follows the standard Stockfish build process.

### Linux / macOS
```bash
cd src
make -j profile-build ARCH=x86-64-avx2
```

### Windows (MinGW/MSYS2)
```bash
cd src
make -j build ARCH=x86-64-bmi2 COMP=mingw
```

### Running via CLI
```bash
./nextfish
uci
isready
position startpos moves e2e4 e7e5
go depth 20
```

---

## 📜 License & Copyright

Nextfish is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License** as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

### Acknowledgements
**Nextfish is a fork of Stockfish.** All credit for the underlying engine architecture, NNUE implementation, and move generation goes to the original Stockfish developers.

*   **Original Authors:** Tord Romstad, Marco Costalba, Joona Kiiski.
*   **Current Maintainers:** The Stockfish Developers (see [AUTHORS](AUTHORS) file).
*   **Nextfish Modifications:** Developed by the Nextfish Strategy Module team.

This program is distributed in the hope that it will be useful, but **WITHOUT ANY WARRANTY**; without even the implied warranty of **MERCHANTABILITY** or **FITNESS FOR A PARTICULAR PURPOSE**. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <http://www.gnu.org/licenses/>.

---
*Based on Stockfish 16.1-dev*