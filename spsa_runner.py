import subprocess
import random
import sys
import re
import json
import os

# --- CẤU HÌNH TUNING ---
PARAMS = {
    "WhiteOptimism":       {"val": 20.85, "min": 0.0,   "max": 40.0,  "step": 1.5},
    "BlackLossPessimism":  {"val": -16.77, "min": -40.0, "max": 0.0,   "step": 1.5},
    "VolatilityThreshold": {"val": 13.83, "min": 5.0,   "max": 30.0,  "step": 0.8},
    "CodeRedLMR":          {"val": 63.31, "min": 40.0,  "max": 95.0,  "step": 1.5},
    "BlackLMR":            {"val": 87.90, "min": 70.0,  "max": 100.0, "step": 1.5},
    "WhiteAggression":     {"val": 25.0,  "min": 10.0,  "max": 50.0,  "step": 2.0}, 
    "PanicTimeFactor":     {"val": 2.0,   "min": 1.1,   "max": 4.0,   "step": 0.2},
    "ComplexityScale":       {"val": 1.0,   "min": 0.8,   "max": 1.5,   "step": 0.05},
    "SoftSingularityMargin": {"val": 0.0,   "min": -20.0, "max": 20.0,  "step": 2.0},
    "TempoBonus":            {"val": 0.0,   "min": -10.0, "max": 30.0,  "step": 2.0},
}

def log(msg):
    print(msg)
    sys.stdout.flush()

def run_fastchess_match(params_A, params_B, games=20):
    opts_A = " ".join([f"option.{k}={v:.4f}" for k, v in params_A.items()])
    opts_B = " ".join([f"option.{k}={v:.4f}" for k, v in params_B.items()])

    cmd = [
        "./fastchess",
        "-engine", "name=Tuned", "cmd=./nextfish", opts_A,
        "-engine", "name=Base",  "cmd=./nextfish", opts_B,
        "-each", "tc=10+0.1", "option.Hash=8", "option.Threads=1", "proto=uci",
        "-rounds", str(games // 2), "-games", "2", "-repeat",
        "-concurrency", "2", "-recover",
        "-openings", "file=UHO_2022_8mvs_+110_+119.pgn", "format=pgn", "order=random"
    ]
    
    # Redirect output to a temp file to avoid pipe buffer issues
    with open("match.log", "w") as f:
        process = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    
    with open("match.log", "r") as f:
        output = f.read()
        
    w = len(re.findall(r"Tuned vs Base\): 1-0", output)) + len(re.findall(r"Base vs Tuned\): 0-1", output))
    l = len(re.findall(r"Tuned vs Base\): 0-1", output)) + len(re.findall(r"Base vs Tuned\): 1-0", output))
    d = len(re.findall(r": 1/2-1/2", output))
    
    return w, l, d

def spsa_optimize(iterations=15):
    log(f"--- INITIALIZING SPSA AUTO-LEARN ({iterations} iterations) ---")
    
    # Check if engine works
    try:
        subprocess.run(["./nextfish", "bench"], capture_output=True, check=True)
        log("Engine Check: OK")
    except:
        log("Engine Check: FAILED (Possible crash or missing net)")
        return

    c = 2.0
    A = 100
    alpha = 0.602
    gamma = 0.101

    for k in range(1, iterations + 1):
        log(f"Iteration {k}/{iterations} starting...")
        ck = c / (k + A)**gamma
        ak = 1.5 / (k + A)**alpha 
        
        delta = {}
        theta_plus = {}
        theta_minus = {}
        
        for name, p in PARAMS.items():
            delta[name] = 1 if random.random() < 0.5 else -1
            change = ck * delta[name] * p["step"]
            theta_plus[name]  = max(p["min"], min(p["max"], p["val"] + change))
            theta_minus[name] = max(p["min"], min(p["max"], p["val"] - change))

        w, l, d = run_fastchess_match(theta_plus, theta_minus)
        total = w + l + d
        if total == 0:
            log(f"  Warning: Iteration {k} failed to produce results.")
            continue

        score_diff = (w - l) / total
        log(f"  Result: {w}W - {l}L - {d}D (Score Diff: {score_diff:.4f})")
        
        for name, p in PARAMS.items():
            gradient = score_diff / (2 * ck * delta[name])
            update = ak * gradient * p["step"] * 20.0 
            p["val"] = max(p["min"], min(p["max"], p["val"] + update))

    final_results = {k: v["val"] for k, v in PARAMS.items()}
    with open("tuned_params.json", "w") as f:
        json.dump(final_results, f)
    log("\n--- SPSA LOOP FINISHED ---")

if __name__ == "__main__":
    iters = int(sys.argv[1]) if len(sys.argv) > 1 else 15
    spsa_optimize(iters)
