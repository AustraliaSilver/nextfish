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
STOCKFISH     = r"D:\nextfish\stockfish18.exe"
ENGINE_EXE    = r"D:\nextfish\nextfish_improved.exe"
BOOK_FILE     = r"D:\nextfish\UHO_2022_8mvs_+110_+119.pgn"
NNUE_FILE     = r"D:\nextfish\nn-c288c895ea92.nnue"

# ─── SPSA Hyper-parameters ────────────────────────────────────────────────────
# Standard SPSA: theta_{k+1} = theta_k + a_k * g_k
# where a_k = a / (k + 1 + A)^alpha,  c_k = c / k^gamma
ALPHA         = 0.602   # standard
GAMMA         = 0.101   # standard
A_CONST       = 10.0    # stability constant (prevents too large initial steps)
A_INIT        = 0.50    # scale of update step (must be large enough to move int params by >=1)
C_INIT        = 0.030   # scale of perturbation (fraction of param range)

# Bounds for each parameter (value stored as int scale for UCI options)
# TM Center: centi-tau (20=0.20, 50=0.50)
# TM Slope: deci-slope (50=5.0, 300=30.0)
# Ext thresholds: thousandths (500=0.500, 950=0.950)
# TM Range: integer percent (85 95-100 115)
PARAM_BOUNDS  = {
    "tm_center":          (20, 50),     # centi-tau
    "tm_slope":           (50, 300),    # deci-slope
    "tm_range_min":       (85, 100),    # integer %
    "tm_range_max":       (100, 115),   # integer %
    "white_threshold":    (500, 950),   # thousandths
    "black_threshold":    (500, 950),   # thousandths
}

# Which UCI option name corresponds to each parameter key
PARAM_TO_UCI = {
    "tm_center":          "HARE TM Center",
    "tm_slope":           "HARE TM Slope",
    "tm_range_min":       "HARE TM Range Min",
    "tm_range_max":       "HARE TM Range Max",
    "white_threshold":    "HARE Ext Threshold White",
    "black_threshold":    "HARE Ext Threshold Black",
}

# Games per candidate evaluation (must be even for balanced opening pairs)
GAMES_PER_EVAL = 40   # 20 rounds × 2 (repeat), higher for better signal
CONCURRENCY    = 2     # reduced to avoid CPU throttling on this machine

# ─── State Management ─────────────────────────────────────────────────────────
DEFAULT_PARAMS = {"tm_center": 35, "tm_slope": 143, "tm_range_min": 95, "tm_range_max": 105, "white_threshold": 823, "black_threshold": 706}

def load_state():
    defaults = {
        "iteration":   0,
        "parameters":  dict(DEFAULT_PARAMS),
        "best_elo":    0.0,
        "best_params": dict(DEFAULT_PARAMS),
    }
    if STATE_FILE.exists():
        try:
            with open(STATE_FILE) as f:
                s = json.load(f)
            for k, v in defaults.items():
                if k not in s:
                    s[k] = dict(DEFAULT_PARAMS) if isinstance(v, dict) else v
            # migrate params to full set (ints)
            for name, val in DEFAULT_PARAMS.items():
                if name in s["parameters"] and isinstance(s["parameters"][name], float):
                    # old format: white_threshold=0.8228 (float) → 823 (int), black_threshold=0.706 → 706
                    s["parameters"][name] = int(round(s["parameters"][name] * 1000))
                elif name not in s["parameters"]:
                    s["parameters"][name] = val
                if name in s["best_params"] and isinstance(s["best_params"][name], float):
                    s["best_params"][name] = int(round(s["best_params"][name] * 1000))
                elif name not in s["best_params"]:
                    s["best_params"][name] = val
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

def param_str(p):
    return f"tmC={p.get('tm_center',0):d} tmS={p.get('tm_slope',0):d} mn={p.get('tm_range_min',0):d} mx={p.get('tm_range_max',0):d} wT={p.get('white_threshold',0):d} bT={p.get('black_threshold',0):d}"

def write_report(state, last_result=None):
    lines = []
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines.append(f"=== SPSA Tuning Report ({ts}) ===")
    lines.append(f"Total iterations completed: {state['iteration']}")
    lines.append(f"Current center:  {param_str(state['parameters'])}")
    lines.append(f"Best params:     {param_str(state['best_params'])}")
    lines.append(f"Best Elo seen:   {state['best_elo']:+.1f}")
    if last_result:
        lines.append("")
        lines.append(f"--- Last Iteration ({state['iteration']}) ---")
        lines.append(f"  (+) {param_str(last_result['plus']['params'])} -> WinRate={last_result['plus']['win_rate']*100:.1f}% {last_result['plus']['score']}")
        lines.append(f"  (-) {param_str(last_result['minus']['params'])} -> WinRate={last_result['minus']['win_rate']*100:.1f}% {last_result['minus']['score']}")
        lines.append(f"  Gradients: {json.dumps(last_result['gradients'], default=lambda x: round(x,4))}")
    lines.append("")
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
                f"  -> {param_str(nc)}"
            )
    with open(REPORT_FILE, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))

def clamp(v, lo, hi):
    return max(lo, min(hi, v))

def cleanup_pgns():
    """Remove temporary PGN files from previous runs."""
    for p in ENGINE_DIR.glob("spsa_iter_*.pgn"):
        try: p.unlink()
        except Exception: pass

# ─── Run Match ────────────────────────────────────────────────────────────────
def run_match(params, pgn_name):
    """Run match with given parameter dict of int UCI option values."""
    cmd = [
        CUTECHESS,
        "-engine", "name=NextfishImproved", f"cmd={ENGINE_EXE}", "dir=D:\\nextfish",
            f"option.EvalFile={NNUE_FILE}",
            "option.Move Overhead=150",
            "option.Use DEE/HARENN=true",
            "option.Use DEE Capture Ordering=true",
            "option.Use DEE Capture LMR=false",
            "option.Use HARE Time Management=true",
            "option.Use HARE Aspiration=true",
            "option.Use HARE Reduction=true",
            "option.Hash=256",
            "proto=uci",
        "-engine", "name=Stockfish", f"cmd={STOCKFISH}",
            "option.Move Overhead=150",
            "option.Hash=128",
            "proto=uci",
    ]
    # Add tunable parameter options
    for key, val in params.items():
        uci_name = PARAM_TO_UCI[key]
        cmd.insert(-1, f"option.{uci_name}={val}")

    cmd += [
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

def evaluate(params, pgn_name):
    w, l, d, elo = run_match(params, pgn_name)
    total = w + l + d
    win_rate = (w + 0.5 * d) / total if total > 0 else 0.5
    return win_rate, w, l, d, elo

# ─── Main SPSA Loop ───────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--max-iter", type=int, default=0, help="Max iterations (0 = run forever)")
    args = parser.parse_args()

    cleanup_pgns()
    state = load_state()
    print(f"\n{'='*50}")
    print(f"SPSA Auto-Tuner Starting")
    print(f"  Current center: {param_str(state['parameters'])}")
    print(f"  Starting from iteration {state['iteration'] + 1}")
    print(f"{'='*50}\n")

    while True:
        k = state["iteration"] + 1
        if args.max_iter > 0 and k > args.max_iter:
            print(f"Reached max iterations ({args.max_iter}). Stopping.")
            break

        a_k = A_INIT / (k + A_CONST) ** ALPHA
        c_k = C_INIT / k ** GAMMA

        cur_params = {k: state["parameters"].get(k, DEFAULT_PARAMS[k]) for k in PARAM_BOUNDS}

        # Shrink bounds around best params
        shrink_factor = max(0.4, 1.0 - (k / 80.0))
        param_info = {}
        param_order = list(PARAM_BOUNDS.keys())
        for name in param_order:
            lo, hi = PARAM_BOUNDS[name]
            cur = cur_params[name]
            best = state["best_params"].get(name, cur)
            span = (hi - lo) * shrink_factor
            lo = max(lo, best - span / 2)
            hi = min(hi, best + span / 2)
            param_info[name] = {"lo": int(round(lo)), "hi": int(round(hi))}

        print(f"\n{'='*50}")
        print(f" ITERATION {k}  a_k={a_k:.5f}  c_k={c_k:.5f}")
        print(f" Current: {param_str(cur_params)}")
        print(f"{'='*50}")

        # Bernoulli perturbation per parameter
        deltas = {name: random.choice([-1, 1]) for name in param_order}

        plus_params  = {}
        minus_params = {}
        for name in param_order:
            cur = cur_params[name]
            lo = param_info[name]["lo"]
            hi = param_info[name]["hi"]
            span = hi - lo
            # perturbation in integer units
            delta_val = max(1, int(round(c_k * span)))
            plus_params[name]  = int(round(clamp(cur + delta_val * deltas[name], lo, hi)))
            minus_params[name] = int(round(clamp(cur - delta_val * deltas[name], lo, hi)))

        # Evaluate (+) candidate
        print(f"\n[+] Evaluating: {param_str(plus_params)}")
        t0 = time.time()
        y_plus, wp, lp, dp, elo_p = evaluate(plus_params, f"spsa_iter_{k}_plus.pgn")
        t_plus = time.time() - t0
        print(f"    => WinRate={y_plus*100:.1f}%  (+{wp} -{lp} ={dp})  Elo={elo_p:+.1f}  [{t_plus/60:.1f} min]")

        # Evaluate (-) candidate
        print(f"\n[-] Evaluating: {param_str(minus_params)}")
        t0 = time.time()
        y_minus, wm, lm, dm, elo_m = evaluate(minus_params, f"spsa_iter_{k}_minus.pgn")
        t_minus = time.time() - t0
        print(f"    => WinRate={y_minus*100:.1f}%  (+{wm} -{lm} ={dm})  Elo={elo_m:+.1f}  [{t_minus/60:.1f} min]")

        # SPSA gradient per parameter
        # Standard SPSA: ĝ = (y₊ - y₋) / (2·cₖ·Δ) in continuous space.
        # For integer params we need |Δθ| ≥ 1. Scale factor bridges the gap:
        # raw_step = aₖ · ĝ · GRAD_SCALE → ~1-2 units at k=1-20.
        GRAD_SCALE = 40.0
        gradients = {}
        new_params = dict(cur_params)
        for name in param_order:
            cur = cur_params[name]
            lo = param_info[name]["lo"]
            hi = param_info[name]["hi"]
            lo_h, hi_h = PARAM_BOUNDS[name]
            span_h = hi_h - lo_h
            delta_val = max(1, int(round(c_k * span_h)))
            raw_gradient = (y_plus - y_minus) / (2.0 * c_k * deltas[name])
            gradients[name] = raw_gradient
            raw_step = a_k * raw_gradient * GRAD_SCALE
            step = int(round(clamp(raw_step, -5, 5)))
            new_val = int(round(clamp(cur + step, lo, hi)))
            new_params[name] = new_val

        # Track best
        mean_elo = (elo_p + elo_m) / 2.0
        if mean_elo > state["best_elo"]:
            state["best_elo"]    = round(mean_elo, 1)
            state["best_params"] = dict(new_params)
            print(f"  ** New best Elo: {mean_elo:+.1f}")

        state["iteration"]       = k
        state["parameters"]      = new_params
        save_state(state)

        last_result = {
            "plus":  {"params": plus_params,  "win_rate": y_plus,  "score": f"+{wp}-{lp}={dp}"},
            "minus": {"params": minus_params, "win_rate": y_minus, "score": f"+{wm}-{lm}={dm}"},
            "gradients": gradients,
        }
        log_iteration({
            "iteration": k,
            "timestamp": datetime.now().isoformat(),
            "old_center": cur_params,
            "plus_candidate":  {**last_result["plus"],  "elo": elo_p},
            "minus_candidate": {**last_result["minus"], "elo": elo_m},
            "gradient": gradients,
            "new_center": new_params,
            "best_elo":   state["best_elo"],
            "best_params": dict(state["best_params"]),
        })

        print(f"\n  [OK] New center: {param_str(new_params)}")
        write_report(state, last_result)


if __name__ == "__main__":
    main()
