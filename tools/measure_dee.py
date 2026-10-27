#!/usr/bin/env python3
"""Run a SPRT match between two DEE configurations.

Usage:
    python3 measure_dee.py --feature lmr --games 2000
    python3 measure_dee.py --feature ordering --games 2000
    python3 measure_dee.py --feature lmr --games 2000
"""
import argparse
import subprocess
import sys
import os
import time

CUTECHESS = "/workspace/.tools/squashfs-root/usr/bin/cutechess-cli"
ENGINE = "/workspace/app/src/stockfish"
SF18 = "/workspace/.tools/sf18/stockfish/stockfish-ubuntu-x86-64-avx2"
BOOK = "/workspace/app/temp/book_fens.json"
NETS = "/workspace/app/src"

FEATURES = {
    "ordering": ("Use DEE Capture Ordering", True, False),
    "pruning": ("Use DEE Capture Pruning", True, False),
    "lmr": ("Use DEE Capture LMR", True, False),
}

def build_engine_block(name, path, feature_on, feature_name):
    """Build cutechess engine block with DEE feature toggled."""
    uci_opt = FEATURES[feature_name][0]
    val = "true" if feature_on else "false"
    block = [
        f"name={name}",
        f"cmd={path}",
        f"dir={os.path.dirname(path)}",
        "option.Use DEE/HARENN=true",
        f"option.{uci_opt}={val}",
        "option.Use DEE Capture Ordering=true",  # baseline: ordering always on
        "option.Use DEE Capture Pruning=false",
        "option.Use DEE Capture LMR=false",
        "option.Hash=256",
        "proto=uci",
    ]
    # Override the specific feature
    if feature_name == "ordering":
        block = [b for b in block if not b.startswith("option.Use DEE Capture Ordering=")]
        block.insert(-1, f"option.Use DEE Capture Ordering={val}")
    elif feature_name == "lmr":
        block = [b for b in block if not b.startswith("option.Use DEE Capture Pruning=")]
        block.insert(-1, f"option.Use DEE Capture Pruning={val}")
    elif feature_name == "lmr":
        block = [b for b in block if not b.startswith("option.Use DEE Capture LMR=")]
        block.insert(-1, f"option.Use DEE Capture LMR={val}")
    return block

def run_sprt(feature, games):
    uci_name, on_val, off_val = FEATURES[feature]
    print(f"\n{'='*60}")
    print(f"SPRT: DEE {feature} (ON vs OFF)")
    print(f"  Feature: {uci_name}")
    print(f"  Games: {games}")
    print(f"{'='*60}\n")

    # Engine A: feature ON
    block_a = build_engine_block("DEE_ON", ENGINE, True, feature)
    # Engine B: feature OFF
    block_b = build_engine_block("DEE_OFF", ENGINE, False, feature)

    cmd = ([CUTECHESS] +
           ["-engine"] + block_a +
           ["-engine"] + block_b +
           ["-each", "tc=0.1+0.01",
            "-rounds", str(games // 2), "-repeat",
            "-concurrency", "4",
            "-openings", f"file={BOOK}", "format=pgn", "order=random",
            "-sprt", "elo0=0", "elo1=5", "alpha=0.05", "beta=0.05",
            "-pgnout", f"/tmp/dee_{feature}.pgn"])

    print(f"Command: {' '.join(cmd[:10])}...")
    print(f"Running SPRT...\n")

    t0 = time.time()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in proc.stdout:
        line = line.strip()
        if line:
            print(line, flush=True)
            if "SPRT:" in line or "Elo difference:" in line or "Finished match" in line:
                print(f"  [t={time.time()-t0:.0f}s]", flush=True)
    proc.wait()
    print(f"\nTotal time: {(time.time()-t0)/60:.1f} minutes")
    print(f"Exit code: {proc.returncode}")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--feature", choices=["ordering", "pruning", "lmr"], required=True)
    parser.add_argument("--games", type=int, default=2000)
    args = parser.parse_args()

    # Ensure book exists
    if not os.path.exists(BOOK):
        print(f"ERROR: Book not found at {BOOK}")
        print("Run: python3 -c \"...\" to generate book_fens.json")
        sys.exit(1)

    run_sprt(args.feature, args.games)

if __name__ == "__main__":
    main()
