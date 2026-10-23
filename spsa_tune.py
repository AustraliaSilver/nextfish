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
import json
import time
import math
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
TC            = "10+0.1"

# ─── SPSA Hyper-parameters ────────────────────────────────────────────────────
# Standard SPSA: theta_{k+1} = theta_k + a_k * g_k
# where a_k = a / (k + 1 + A)^alpha,  c_k = c / k^gamma
ALPHA         = 0.602   # standard
GAMMA         = 0.101   # standard
A_CONST       = 10.0    # stability constant (prevents too large initial steps)
A_INIT        = 0.50    # scale of update step
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

# Hard timeout for a single cutechess match
MATCH_TIMEOUT  = 3600

# Gradient scaling factor and relative step bound (fraction of the parameter span)
GRAD_SCALE      = 40.0
REL_STEP_BOUND  = 0.10

# ─── State Management ─────────────────────────────────────────────────────────
DEFAULT_PARAMS = {"tm_center": 35, "tm_slope": 143, "tm_range_min": 95, "tm_range_max": 105, "white_threshold": 823, "black_threshold": 706}

# Pre-compiled regular expressions for performance
SCORE_RE = re.compile(r"Score of NextfishImproved vs Stockfish:\s+(\d+)\s+-\s+(\d+)\s+-\s+(\d+)")
ELO_RE = re.compile(r"Elo difference:\s*([-+]?(?:inf|\d*\.?\d+))", re.IGNORECASE)


class MatchError(RuntimeError):
    """Raised when a match could not be evaluated (timeout, crash, no games)."""


def load_state():
    defaults = {
        "iteration":   0,
        "parameters":  dict(DEFAULT_PARAMS),
        "best_elo":    float("-inf"),
        "best_params": dict(DEFAULT_PARAMS),
        "best_win_rate": -1.0,
    }
    if not STATE_FILE.exists():
        return defaults

    with open(STATE_FILE) as f:
        try:
            s = json.load(f)
        except json.JSONDecodeError as e:
            raise RuntimeError(f"Corrupt state file {STATE_FILE}: {e}") from e

    for k, v in defaults.items():
        if k not in s:
            s[k] = dict(DEFAULT_PARAMS) if isinstance(v, dict) else v

    # migrate params to full set (ints) and clamp them into their bounds
    for name, val in DEFAULT_PARAMS.items():
        lo, hi = PARAM_BOUNDS[name]
        for section in ("parameters", "best_params"):
            if section not in s or not isinstance(s[section], dict):
                s[section] = dict(DEFAULT_PARAMS)
            raw = s[section].get(name, val)
            if isinstance(raw, float):
                # old format: white_threshold=0.8228 (float) → 823 (int), black_threshold=0.706 → 706
                raw = int(round(raw * 1000))
            s[section][name] = int(clamp(int(raw), lo, hi))

    if s.get("best_elo") is None:
        s["best_elo"] = float("-inf")
    return s

def save_state(state):
    tmp_file = STATE_FILE.with_name(STATE_FILE.name + ".tmp")
    with open(tmp_file, "w") as f:
        json.dump(state, f, indent=4)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp_file, STATE_FILE)

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

# ─── Openings ─────────────────────────────────────────────────────────────────
def build_openings_file(iteration, out_path):
    """Deterministically pick a slice of the book, identical for both candidates."""
    games = []
    block = []
    with open(BOOK_FILE, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            block.append(line)
            if not line.strip():
                if any(l.startswith("[Event") for l in block):
                    games.append("".join(block))
                block = []
    if block and any(l.startswith("[Event") for l in block):
        games.append("".join(block))

    if not games:
        raise MatchError(f"Opening book {BOOK_FILE} does not contain any game")

    rng = random.Random(iteration)
    rng.shuffle(games)

    needed = max(1, GAMES_PER_EVAL // 2)
    chosen = []
    while len(chosen) < needed:
        chosen.extend(games)
    chosen = chosen[:needed]

    with open(out_path, "w", encoding="utf-8") as f:
        for game in chosen:
            f.write(game if game.endswith("\n") else game + "\n")
            f.write("\n")
    return out_path

# ─── Run Match ────────────────────────────────────────────────────────────────
def run_match(params, pgn_name, openings_file):
    """Run match with given parameter dict of int UCI option values."""
    nextfish_block = [
        "name=NextfishImproved", f"cmd={ENGINE_EXE}", f"dir={ENGINE_DIR}",
        "option.Use DEE/HARENN=true",
        "option.Use DEE Capture Ordering=true",
        "option.Use DEE Capture LMR=false",
        "option.Use DEE Capture Pruning=false",
        "option.Use HARE Time Management=true",
        "option.Hash=256",
        "proto=uci",
    ]

    # Add tunable parameter options to the Nextfish engine block only
    for key, val in params.items():
        uci_name = PARAM_TO_UCI[key]
        nextfish_block.insert(-1, f"option.{uci_name}={val}")

    stockfish_block = [
        "name=Stockfish", f"cmd={STOCKFISH}",
        "option.Hash=128",
        "proto=uci",
    ]

    cmd = [CUTECHESS, "-engine"] + nextfish_block + ["-engine"] + stockfish_block + [
        "-each", f"tc={TC}",
        "-rounds", str(GAMES_PER_EVAL // 2), "-repeat",
        "-concurrency", str(CONCURRENCY),
        "-openings", f"file={openings_file}", "format=pgn", "order=sequential",
        "-pgnout", str(ENGINE_DIR / pgn_name),
    ]

    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=MATCH_TIMEOUT)
    except subprocess.TimeoutExpired:
        raise MatchError(f"cutechess timed out after {MATCH_TIMEOUT} seconds")

    if r.returncode != 0:
        raise MatchError(f"cutechess failed with return code {r.returncode}: {r.stderr[-500:]}")

    wins = losses = draws = 0
    elo = 0.0
    for line in r.stdout.splitlines():
        m = SCORE_RE.search(line)
        if m:
            wins, losses, draws = int(m.group(1)), int(m.group(2)), int(m.group(3))
        m = ELO_RE.search(line)
        if m:
            elo = parse_elo(m.group(1))

    if wins + losses + draws == 0:
        raise MatchError("cutechess produced no games (w=l=d=0)")
    return wins, losses, draws, elo

def parse_elo(text):
    """Parse an Elo value, handling the +/-inf values cutechess prints."""
    value = text.strip().lower()
    if value in ("inf", "+inf"):
        return float("inf")
    if value == "-inf":
        return float("-inf")
    try:
        return float(value)
    except ValueError:
        return 0.0

def evaluate(params, pgn_name, openings_file):
    w, l, d, elo = run_match(params, pgn_name, openings_file)
    total = w + l + d
    if total <= 0:
        raise MatchError(f"no games played for {pgn_name}")
    win_rate = (w + 0.5 * d) / total
    return win_rate, w, l, d, elo

# ─── Main SPSA Loop ───────────────────────────────────────────────────────────
def main():
    global CUTECHESS, STOCKFISH, ENGINE_EXE, ENGINE_DIR, SRC_DIR, STATE_FILE, HISTORY_FILE
    global REPORT_FILE, BOOK_FILE, TC, GAMES_PER_EVAL, CONCURRENCY

    parser = argparse.ArgumentParser(description="SPSA tuner for the HARENN parameters")
    parser.add_argument("--max-iter", type=int, default=0, help="Max iterations (0 = run forever)")
    parser.add_argument("--cutechess", default=CUTECHESS, help="Path to the cutechess-cli binary")
    parser.add_argument("--engine", default=ENGINE_EXE, help="Path to the Nextfish binary")
    parser.add_argument("--engine-dir", default=str(ENGINE_DIR),
                        help="Working directory of the engine. It has to contain nextfish.harenn "
                             "and the NNUE nets, the engine resolves them relative to its own "
                             "directory.")
    parser.add_argument("--stockfish", default=STOCKFISH, help="Path to the Stockfish 18 binary")
    parser.add_argument("--book", default=BOOK_FILE, help="Path to the opening book in PGN format")
    parser.add_argument("--tc", default=TC,
                        help="Time control of a single game, for example 2+0.1. A short time "
                             "control makes an iteration much faster at the price of a noisier "
                             "gradient.")
    parser.add_argument("--games-per-eval", type=int, default=GAMES_PER_EVAL,
                        help="Number of games per evaluated candidate")
    parser.add_argument("--concurrency", type=int, default=CONCURRENCY,
                        help="Number of games played in parallel by cutechess")
    args = parser.parse_args()

    CUTECHESS = args.cutechess
    ENGINE_EXE = args.engine
    STOCKFISH = args.stockfish
    BOOK_FILE = args.book
    TC = args.tc
    GAMES_PER_EVAL = args.games_per_eval
    CONCURRENCY = args.concurrency

    ENGINE_DIR = Path(args.engine_dir)
    SRC_DIR = ENGINE_DIR / "src"
    STATE_FILE = ENGINE_DIR / "spsa_tuning_state.json"
    HISTORY_FILE = ENGINE_DIR / "spsa_tuning_history.jsonl"
    REPORT_FILE = ENGINE_DIR / "spsa_tuning_report.txt"

    for path, what in ((CUTECHESS, "cutechess"), (ENGINE_EXE, "engine"), (STOCKFISH, "stockfish"),
                       (BOOK_FILE, "opening book")):
        if not Path(path).exists():
            print(f"error: the {what} '{path}' does not exist")
            return 1
    if not ENGINE_DIR.is_dir():
        print(f"error: the engine directory '{ENGINE_DIR}' does not exist")
        return 1

    print(f"\n{'='*50}")
    print(f"SPSA Auto-Tuner Starting")
    print(f"  cutechess:   {CUTECHESS}")
    print(f"  engine:      {ENGINE_EXE}")
    print(f"  stockfish:   {STOCKFISH}")
    print(f"  engine dir:  {ENGINE_DIR}")
    print(f"  book:        {BOOK_FILE}")
    print(f"  time control:{TC}")
    print(f"  games/eval:  {GAMES_PER_EVAL}")
    print(f"{'='*50}\n")

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

        cur_params = {name: state["parameters"].get(name, DEFAULT_PARAMS[name]) for name in PARAM_BOUNDS}

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

        # Same openings for both candidates of this iteration
        openings_file = build_openings_file(k, ENGINE_DIR / f"spsa_iter_{k}_openings.pgn")

        # Bernoulli perturbation per parameter
        deltas = {name: random.choice([-1, 1]) for name in param_order}

        plus_params  = {}
        minus_params = {}
        applied_delta = {}
        for name in param_order:
            cur = cur_params[name]
            lo = param_info[name]["lo"]
            hi = param_info[name]["hi"]
            span = hi - lo
            # perturbation in integer units
            delta_val = max(1, int(round(c_k * span)))
            applied_delta[name] = delta_val
            plus_params[name]  = int(round(clamp(cur + delta_val * deltas[name], lo, hi)))
            minus_params[name] = int(round(clamp(cur - delta_val * deltas[name], lo, hi)))

        # Evaluate (+) candidate
        print(f"\n[+] Evaluating: {param_str(plus_params)}")
        t0 = time.time()
        try:
            y_plus, wp, lp, dp, elo_p = evaluate(plus_params, f"spsa_iter_{k}_plus.pgn", openings_file)
        except MatchError as e:
            print(f"    !! Aborting iteration: {e}")
            break
        t_plus = time.time() - t0
        print(f"    => WinRate={y_plus*100:.1f}%  (+{wp} -{lp} ={dp})  Elo={elo_p:+.1f}  [{t_plus/60:.1f} min]")

        # Evaluate (-) candidate
        print(f"\n[-] Evaluating: {param_str(minus_params)}")
        t0 = time.time()
        try:
            y_minus, wm, lm, dm, elo_m = evaluate(minus_params, f"spsa_iter_{k}_minus.pgn", openings_file)
        except MatchError as e:
            print(f"    !! Aborting iteration: {e}")
            break
        t_minus = time.time() - t0
        print(f"    => WinRate={y_minus*100:.1f}%  (+{wm} -{lm} ={dm})  Elo={elo_m:+.1f}  [{t_minus/60:.1f} min]")

        # SPSA gradient per parameter
        # Standard SPSA: g = (y₊ - y₋) / (2·Δθ·Δ) with Δθ the applied perturbation.
        # The step is scaled by the parameter span so that every parameter moves
        # by the same relative amount, and is bounded relative to its own span.
        gradients = {}
        new_params = dict(cur_params)
        for name in param_order:
            cur = cur_params[name]
            lo = param_info[name]["lo"]
            hi = param_info[name]["hi"]
            span = max(1, hi - lo)
            delta_val = applied_delta[name]
            raw_gradient = (y_plus - y_minus) / (2.0 * delta_val * deltas[name])
            gradients[name] = raw_gradient
            raw_step = a_k * raw_gradient * GRAD_SCALE * span
            step = int(round(clamp(raw_step, -REL_STEP_BOUND * span, REL_STEP_BOUND * span)))
            new_val = int(round(clamp(cur + step, lo, hi)))
            new_params[name] = new_val

        # Track best (only parameters that were actually measured are eligible)
        mean_elo = (elo_p + elo_m) / 2.0
        mean_wr = (y_plus + y_minus) / 2.0
        best_candidate = plus_params if y_plus >= y_minus else minus_params
        prev_elo = state.get("best_elo", float("-inf"))
        prev_wr = state.get("best_win_rate", -1.0)
        better = mean_elo > prev_elo
        if not better and not math.isfinite(mean_elo) and not math.isfinite(prev_elo):
            better = mean_wr > prev_wr
        if better:
            state["best_elo"]      = round(mean_elo, 1)
            state["best_params"]   = dict(best_candidate)
            state["best_win_rate"] = mean_wr
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
