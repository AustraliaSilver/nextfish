import subprocess
import random
import sys
import re
import json
import os

# Định nghĩa các tham số cần tune
PARAMS_CONFIG = {
    "WhiteOptimism":       {"min": 0.0,   "max": 50.0,  "step": 1.0},
    "BlackLossPessimism":  {"min": -50.0, "max": 0.0,   "step": 1.0},
    "BlackEqualPessimism": {"min": -20.0, "max": 10.0,  "step": 1.0},
    "VolatilityThreshold": {"min": 5.0,   "max": 30.0,  "step": 0.5},
    "CodeRedLMR":          {"min": 40.0,  "max": 95.0,  "step": 1.0},
    "BlackLMR":            {"min": 70.0,  "max": 100.0, "step": 1.0},
    "WhiteAggression":     {"min": 10.0,  "max": 60.0,  "step": 1.5}, 
    "PanicTimeFactor":     {"min": 1.0,   "max": 4.0,   "step": 0.1},
    "ComplexityScale":     {"min": 0.5,   "max": 2.0,   "step": 0.05},
    "SoftSingularityMargin": {"min": -20.0, "max": 20.0,  "step": 1.0},
    "TempoBonus":          {"min": -10.0, "max": 30.0,  "step": 1.0},
}

def get_current_params_from_cpp(file_path="src/nextfish_strategy.cpp"):
    params = {}
    with open(file_path, "r") as f:
        content = f.read()
    for name in PARAMS_CONFIG:
        match = re.search(r"double\s+" + name + r"\s*=\s*([-+]?[0-9]*\.?[0-9]+)\s*;", content)
        if match:
            params[name] = float(match.group(1))
        else:
            params[name] = 0.0 # Default if not found
    return params

def log(msg):
    print(msg)
    sys.stdout.flush()

def run_match(params_A, params_B, games=20):
    if not os.path.exists("./fastchess"):
        log("ERROR: ./fastchess not found!")
        return 0, 0, 0
    opts_A = " ".join([f"option.{k}={v:.4f}" for k, v in params_A.items()])
    opts_B = " ".join([f"option.{k}={v:.4f}" for k, v in params_B.items()])
    cmd = [
        "./fastchess", "-engine", "name=Tuned", "cmd=./nextfish", opts_A,
        "-engine", "name=Base", "cmd=./nextfish", opts_B,
        "-each", "tc=10+0.1", "option.Hash=8", "option.Threads=1", "proto=uci",
        "-rounds", str(games // 2), "-games", "2", "-repeat", "-concurrency", "2", "-recover",
        "-openings", "file=UHO_2022_8mvs_+110_+119.pgn", "format=pgn", "order=random"
    ]
    with open("match.log", "w") as f:
        subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    with open("match.log", "r") as f:
        output = f.read()
    w = len(re.findall(r"Tuned vs Base\): 1-0", output)) + len(re.findall(r"Base vs Tuned\): 0-1", output))
    l = len(re.findall(r"Tuned vs Base\): 0-1", output)) + len(re.findall(r"Base vs Tuned\): 1-0", output))
    d = len(re.findall(r": 1/2-1/2", output))
    return w, l, d

def spsa_optimize(iterations=1, start_k=1):
    log(f"--- INITIALIZING SPSA AUTO-LEARN (Step {start_k}) ---")
    current_values = get_current_params_from_cpp()
    log(f"Starting values: {current_values}")

    c = 2.0
    A = 100
    alpha = 0.602
    gamma = 0.101

    for k in range(start_k, start_k + iterations):
        log(f"Iteration {k} starting...")
        ck = c / (k + A)**gamma
        ak = 1.0 / (k + A)**alpha 
        delta, theta_plus, theta_minus = {}, {}, {}
        
        for name, val in current_values.items():
            step = PARAMS_CONFIG[name]["step"]
            delta[name] = 1 if random.random() < 0.5 else -1
            change = ck * delta[name] * step
            theta_plus[name]  = max(PARAMS_CONFIG[name]["min"], min(PARAMS_CONFIG[name]["max"], val + change))
            theta_minus[name] = max(PARAMS_CONFIG[name]["min"], min(PARAMS_CONFIG[name]["max"], val - change))

        w, l, d = run_match(theta_plus, theta_minus)
        total = w + l + d
        if total == 0: continue
        score_diff = (w - l) / total
        log(f"  Result: {w}W - {l}L - {d}D (Diff: {score_diff:.4f})")
        
        for name in current_values:
            gradient = score_diff / (2 * ck * delta[name])
            update = ak * gradient * PARAMS_CONFIG[name]["step"] * 10.0
            current_values[name] = max(PARAMS_CONFIG[name]["min"], min(PARAMS_CONFIG[name]["max"], current_values[name] + update))

    with open("tuned_params.json", "w") as f:
        json.dump(current_values, f)
    log("\n--- SPSA LOOP FINISHED ---")

if __name__ == "__main__":
    iter_count = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    start_k = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    spsa_optimize(iter_count, start_k)