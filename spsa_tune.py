# -*- coding: utf-8 -*-
"""
SPSA Auto-Tuner for Nextfish HARENN Selective Depth Extension Thresholds.
Runs continuously, self-adjusting step sizes, logging all results.

Parameters being tuned:
  - white_threshold: rho/rs threshold for White-to-move positions  [0.50, 0.95]
  - black_threshold: rho/rs threshold for Black-to-move positions  [0.50, 0.95]

Usage:
  python spsa_tune.py                 # run until manually stopped
  python spsa_tune.py --max-iter 20   # run for N iterations then stop
"""

import os
import re
import sys
import json
import time
import shutil
import random
import argparse
import subprocess
from pathlib import Path
from datetime import datetime

# ─── Paths ────────────────────────────────────────────────────────────────────
SRC_DIR       = Path("D:/nextfish/src")
ENGINE_DIR    = Path("D:/nextfish")
STATE_FILE    = ENGINE_DIR / "spsa_tuning_state.json"
HISTORY_FILE  = ENGINE_DIR / "spsa_tuning_history.jsonl"
REPORT_FILE   = ENGINE_DIR / "spsa_tuning_report.txt"

CUTECHESS     = r"C:\Program Files (x86)\Cute Chess\cutechess-cli.exe"
STOCKFISH     = r"C:\Users\Admin\Downloads\stockfish-windows-x86-64-avx2\stockfish\stockfish-windows-x86-64-avx2.exe"
ENGINE_EXE    = r"D:\nextfish\nextfish_improved.exe"
BOOK_FILE     = r"D:\nextfish\UHO_2022_8mvs_+110_+119.pgn"
NNUE_FILE     = r"D:\nextfish\nn-c288c895ea92.nnue"

# ─── SPSA Hyper-parameters ────────────────────────────────────────────────────
# Standard SPSA: theta_{k+1} = theta_k + a_k * g_k
# where a_k = a / (k + 1 + A)^alpha,  c_k = c / k^gamma
ALPHA         = 0.602   # standard
GAMMA         = 0.101   # standard
A_CONST       = 10.0    # stability constant (prevents too large initial steps)
A_INIT        = 0.025   # scale of update step (smaller = more conservative)
C_INIT        = 0.040   # scale of perturbation (fraction of param range)

# Bounds for each parameter (can shrink dynamically as tuning progresses)
PARAM_BOUNDS  = {
    "white_threshold": (0.55, 0.92),
    "black_threshold": (0.50, 0.88),
}

# Games per candidate evaluation (must be even for balanced opening pairs)
GAMES_PER_EVAL = 20   # 10 rounds × 2 (repeat)
CONCURRENCY    = 4

# ─── State Management ─────────────────────────────────────────────────────────
def load_state():
    defaults = {
        "iteration":   0,
        "parameters":  {"white_threshold": 0.790, "black_threshold": 0.690},
        "best_elo":    0.0,
        "best_params": {"white_threshold": 0.790, "black_threshold": 0.690},
    }
    if STATE_FILE.exists():
        try:
            with open(STATE_FILE) as f:
                s = json.load(f)
            # fill missing keys
            for k, v in defaults.items():
                if k not in s:
                    s[k] = v
            return s
        except Exception:
            pass
    return defaults

def save_state(state):
    with open(STATE_FILE, "w") as f:
        json.dump(state, f, indent=4)

def log_iteration(data):
    with open(HISTORY_FILE, "a") as f:
        f.write(json.dumps(data, ensure_ascii=False) + "\n")

def write_report(state, last_result=None):
    lines = []
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines.append(f"=== SPSA Tuning Report ({ts}) ===")
    lines.append(f"Total iterations completed: {state['iteration']}")
    lines.append(f"Current center:  White={state['parameters']['white_threshold']:.3f}, Black={state['parameters']['black_threshold']:.3f}")
    lines.append(f"Best params:     White={state['best_params']['white_threshold']:.3f}, Black={state['best_params']['black_threshold']:.3f}")
    lines.append(f"Best Elo seen:   {state['best_elo']:+.1f}")
    if last_result:
        lines.append("")
        lines.append(f"--- Last Iteration ({state['iteration']}) ---")
        lines.append(f"  (+) W={last_result['plus']['white']:.3f} B={last_result['plus']['black']:.3f} -> WinRate={last_result['plus']['win_rate']*100:.1f}% {last_result['plus']['score']}")
        lines.append(f"  (-) W={last_result['minus']['white']:.3f} B={last_result['minus']['black']:.3f} -> WinRate={last_result['minus']['win_rate']*100:.1f}% {last_result['minus']['score']}")
        lines.append(f"  Gradient: dW={last_result['grad_w']:+.4f}, dB={last_result['grad_b']:+.4f}")
    lines.append("")
    # Last 5 iterations from history
    if HISTORY_FILE.exists():
        all_iter = []
        with open(HISTORY_FILE) as f:
            for line in f:
                try:
                    all_iter.append(json.loads(line))
                except Exception:
                    pass
        lines.append("--- Last 5 iterations ---")
        for it in all_iter[-5:]:
            nc = it.get("new_center", {})
            p = it.get("plus_candidate", {})
            m = it.get("minus_candidate", {})
            lines.append(
                f"  Iter {it['iteration']:3d}: (+){p.get('win_rate',0)*100:.0f}% / (-){m.get('win_rate',0)*100:.0f}%"
                f"  -> W={nc.get('white',0):.3f} B={nc.get('black',0):.3f}"
            )
    with open(REPORT_FILE, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))

# ─── Source Code Patching ─────────────────────────────────────────────────────
def clamp(v, lo, hi):
    return max(lo, min(hi, v))

def patch_source(white_val, black_val):
    # search.cpp
    search_cpp = SRC_DIR / "search.cpp"
    txt = search_cpp.read_text(encoding="utf-8")
    txt = re.sub(
        r"const float threshold = \(us == BLACK\) \? [\d\.]+f : [\d\.]+f;",
        f"const float threshold = (us == BLACK) ? {black_val:.4f}f : {white_val:.4f}f;",
        txt,
    )
    search_cpp.write_text(txt, encoding="utf-8", newline="\n")

    # harenn_ctrl.cpp
    ctrl_cpp = SRC_DIR / "harenn_ctrl.cpp"
    txt = ctrl_cpp.read_text(encoding="utf-8")
    txt = re.sub(
        r"const float threshold = isBlack \? [\d\.]+f : [\d\.]+f;",
        f"const float threshold = isBlack ? {black_val:.4f}f : {white_val:.4f}f;",
        txt,
    )
    ctrl_cpp.write_text(txt, encoding="utf-8", newline="\n")

# ─── Build Engine ─────────────────────────────────────────────────────────────
def clean_and_build():
    # Clean
    for p in SRC_DIR.rglob("*.o"):
        try: p.unlink()
        except Exception: pass
    for p in SRC_DIR.glob("*.exe"):
        try: p.unlink()
        except Exception: pass

    env = os.environ.copy()
    msys_bin = "C:\\msys64\\usr\\bin"
    if msys_bin not in env.get("PATH", ""):
        env["PATH"] = msys_bin + os.pathsep + env["PATH"]

    r = subprocess.run(
        ["make", "profile-build", "ARCH=x86-64-avx2", "COMP=mingw", "-j4"],
        cwd=SRC_DIR, shell=True, env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    if r.returncode != 0:
        return False

    compiled = SRC_DIR / "stockfish.exe"
    if compiled.exists():
        shutil.copy(compiled, ENGINE_EXE)
        return True
    return False

# ─── Run Match ────────────────────────────────────────────────────────────────
def run_match(pgn_name):
    cmd = [
        CUTECHESS,
        "-engine", "name=NextfishImproved", f"cmd={ENGINE_EXE}", "dir=D:\\nextfish",
            f"option.EvalFile={NNUE_FILE}",
            "option.Move Overhead=150",
            "option.Use DEE/HARENN=true",
            "option.Use DEE Capture Ordering=false",
            "option.Use DEE Capture LMR=false",
            "option.Use HARE Time Management=false",
            "option.Use HARE Aspiration=false",
            "option.Use HARE Reduction=false",
            "option.Hash=256",
            "proto=uci",
        "-engine", "name=Stockfish", f"cmd={STOCKFISH}",
            "option.Move Overhead=150",
            "option.Hash=128",
            "proto=uci",
        "-each", "tc=10+0.1",
        "-rounds", str(GAMES_PER_EVAL // 2), "-repeat",
        "-concurrency", str(CONCURRENCY),
        "-openings", f"file={BOOK_FILE}", "format=pgn", "order=random",
        "-pgnout", str(ENGINE_DIR / pgn_name),
    ]

    r = subprocess.run(cmd, capture_output=True, text=True)
    wins = losses = draws = 0
    elo = 0.0
    for line in r.stdout.splitlines():
        m = re.search(r"Score of NextfishImproved vs Stockfish:\s+(\d+)\s+-\s+(\d+)\s+-\s+(\d+)", line)
        if m:
            wins, losses, draws = int(m.group(1)), int(m.group(2)), int(m.group(3))
        m = re.search(r"Elo difference:\s+([-+]?\d*\.?\d+)", line)
        if m:
            elo = float(m.group(1))
    return wins, losses, draws, elo

def evaluate(white_val, black_val, pgn_name):
    patch_source(white_val, black_val)
    if not clean_and_build():
        print(f"  [ERROR] Build failed for W={white_val:.3f} B={black_val:.3f}")
        return 0.5, 0, 0, 0, 0.0
    w, l, d, elo = run_match(pgn_name)
    total = w + l + d
    win_rate = (w + 0.5 * d) / total if total > 0 else 0.5
    return win_rate, w, l, d, elo

# ─── Main SPSA Loop ───────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--max-iter", type=int, default=0, help="Max iterations (0 = run forever)")
    args = parser.parse_args()

    state = load_state()
    print(f"\n{'='*50}")
    print(f"SPSA Auto-Tuner Starting")
    print(f"  Current center: White={state['parameters']['white_threshold']:.3f}, Black={state['parameters']['black_threshold']:.3f}")
    print(f"  Starting from iteration {state['iteration'] + 1}")
    print(f"{'='*50}\n")

    while True:
        k = state["iteration"] + 1
        if args.max_iter > 0 and k > args.max_iter:
            print(f"Reached max iterations ({args.max_iter}). Stopping.")
            break

        # Compute step sizes (decay automatically)
        a_k = A_INIT / (k + A_CONST) ** ALPHA
        # Automatically adjust perturbation c_k dynamically based on iteration depth to make finer adjustments
        c_k = C_INIT / k ** GAMMA

        cur_w = state["parameters"]["white_threshold"]
        cur_b = state["parameters"]["black_threshold"]

        # Dynamically shrink parameter bounds around the best known parameters as tuning progresses
        # after iteration 15, to focus search on the promising region and avoid wild jumps.
        best_w = state["best_params"].get("white_threshold", cur_w)
        best_b = state["best_params"].get("black_threshold", cur_b)
        
        # We allow the search interval to shrink as iterations increase (e.g. up to 50% smaller at iter 50)
        shrink_factor = max(0.4, 1.0 - (k / 80.0)) 
        orig_lo_w, orig_hi_w = PARAM_BOUNDS["white_threshold"]
        orig_lo_b, orig_hi_b = PARAM_BOUNDS["black_threshold"]
        
        w_span = (orig_hi_w - orig_lo_w) * shrink_factor
        b_span = (orig_hi_b - orig_lo_b) * shrink_factor
        
        lo_w = max(orig_lo_w, best_w - w_span / 2)
        hi_w = min(orig_hi_w, best_w + w_span / 2)
        lo_b = max(orig_lo_b, best_b - b_span / 2)
        hi_b = min(orig_hi_b, best_b + b_span / 2)

        print(f"\n{'='*50}")
        print(f" ITERATION {k}  a_k={a_k:.5f}  c_k={c_k:.5f}")
        print(f" Center: White={cur_w:.3f}  Black={cur_b:.3f}")
        print(f" Active Bounds: White [{lo_w:.3f}, {hi_w:.3f}], Black [{lo_b:.3f}, {hi_b:.3f}]")
        print(f"{'='*50}")

        # Bernoulli perturbation vector
        delta_w = random.choice([-1, 1])
        delta_b = random.choice([-1, 1])

        w_plus  = round(clamp(cur_w + c_k * delta_w, lo_w, hi_w), 4)
        b_plus  = round(clamp(cur_b + c_k * delta_b, lo_b, hi_b), 4)
        w_minus = round(clamp(cur_w - c_k * delta_w, lo_w, hi_w), 4)
        b_minus = round(clamp(cur_b - c_k * delta_b, lo_b, hi_b), 4)

        # Evaluate (+) candidate
        print(f"\n[+] Evaluating: White={w_plus:.4f}  Black={b_plus:.4f}")
        t0 = time.time()
        y_plus, wp, lp, dp, elo_p = evaluate(w_plus, b_plus, f"spsa_iter_{k}_plus.pgn")
        t_plus = time.time() - t0
        print(f"    => WinRate={y_plus*100:.1f}%  (+{wp} -{lp} ={dp})  Elo={elo_p:+.1f}  [{t_plus/60:.1f} min]")

        # Evaluate (-) candidate
        print(f"\n[-] Evaluating: White={w_minus:.4f}  Black={b_minus:.4f}")
        t0 = time.time()
        y_minus, wm, lm, dm, elo_m = evaluate(w_minus, b_minus, f"spsa_iter_{k}_minus.pgn")
        t_minus = time.time() - t0
        print(f"    => WinRate={y_minus*100:.1f}%  (+{wm} -{lm} ={dm})  Elo={elo_m:+.1f}  [{t_minus/60:.1f} min]")

        # SPSA gradient estimate → maximize win_rate
        grad_w = (y_plus - y_minus) / (2.0 * c_k * delta_w)
        grad_b = (y_plus - y_minus) / (2.0 * c_k * delta_b)

        new_w = round(clamp(cur_w + a_k * grad_w, lo_w, hi_w), 4)
        new_b = round(clamp(cur_b + a_k * grad_b, lo_b, hi_b), 4)

        # Track best (use mean elo of two evaluations as proxy)
        mean_elo = (elo_p + elo_m) / 2.0
        if mean_elo > state["best_elo"]:
            state["best_elo"]    = round(mean_elo, 1)
            state["best_params"] = {"white_threshold": new_w, "black_threshold": new_b}
            print(f"  ** New best Elo: {mean_elo:+.1f}")

        state["iteration"]                        = k
        state["parameters"]["white_threshold"]    = new_w
        state["parameters"]["black_threshold"]    = new_b
        save_state(state)

        last_result = {
            "plus":  {"white": w_plus,  "black": b_plus,  "win_rate": y_plus,  "score": f"+{wp}-{lp}={dp}"},
            "minus": {"white": w_minus, "black": b_minus, "win_rate": y_minus, "score": f"+{wm}-{lm}={dm}"},
            "grad_w": grad_w, "grad_b": grad_b,
        }
        log_iteration({
            "iteration": k,
            "timestamp": datetime.now().isoformat(),
            "old_center": {"white": cur_w, "black": cur_b},
            "plus_candidate":  {**last_result["plus"],  "elo": elo_p},
            "minus_candidate": {**last_result["minus"], "elo": elo_m},
            "gradient": {"white": grad_w, "black": grad_b},
            "new_center": {"white": new_w, "black": new_b},
            "best_elo":   state["best_elo"],
            "best_params": state["best_params"],
        })

        print(f"\n  [OK] New center: White={new_w:.4f}  Black={new_b:.4f}")
        write_report(state, last_result)


if __name__ == "__main__":
    main()
