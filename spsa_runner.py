import subprocess
import random
import sys
import re

# Configuration
PARAMS = {
    "WhiteOptimism":       {"val": 20, "min": 0,   "max": 40,  "step": 2.0},
    "BlackLossPessimism":  {"val": -15, "min": -40, "max": 0,   "step": 2.0},
    "VolatilityThreshold": {"val": 14, "min": 5,   "max": 30,  "step": 1.0},
    "CodeRedLMR":          {"val": 65, "min": 40,  "max": 95,  "step": 2.0},
    "BlackLMR":            {"val": 88, "min": 70,  "max": 100, "step": 2.0},
}

# SPSA Constants
a = 2.0
c = 4.0
A = 100
alpha = 0.602
gamma = 0.101

def run_fastchess_match(params_A, params_B, games_per_iter=4):
    """Runs a match using fastchess between Engine A and Engine B"""
    
    # Format options for fastchess: name=key value=val
    opts_A = " ".join([f"option.{k}={int(v)}" for k, v in params_A.items()])
    opts_B = " ".join([f"option.{k}={int(v)}" for k, v in params_B.items()])

    # Fastchess command construction
    cmd = [
        "./fastchess",
        "-engine", "name=Nextfish_A", "cmd=./nextfish", opts_A,
        "-engine", "name=Nextfish_B", "cmd=./nextfish", opts_B,
        "-each", "tc=1+0.02", "option.Hash=8", "option.Threads=1", "proto=uci",
        "-rounds", str(games_per_iter // 2),
        "-games", "2",
        "-repeat",
        "-concurrency", "2",
        "-recover",
        "-pgnout", "file=spsa_chunk.pgn"
    ]
    
    print(f"Running match: {' '.join(cmd[:10])} ...")
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        output = result.stdout + result.stderr
        
        # Manually count results from individual game lines
        # Format: "Finished game 1 (Nextfish_A vs Nextfish_B): 1-0 {White mates}"
        w = len(re.findall(r"Nextfish_A vs Nextfish_B\): 1-0", output)) + \
            len(re.findall(r"Nextfish_B vs Nextfish_A\): 0-1", output))
            
        l = len(re.findall(r"Nextfish_A vs Nextfish_B\): 0-1", output)) + \
            len(re.findall(r"Nextfish_B vs Nextfish_A\): 1-0", output))
            
        d = len(re.findall(r": 1/2-1/2", output))
        
        if (w + l + d) > 0:
            return w, l, d
        else:
            print("Warning: No game results found in output. Log snippet:")
            print(output[-300:])
            return 0, 0, 0
            
    except Exception as e:
        print(f"Error executing fastchess: {e}")
        return 0, 0, 0

def spsa_optimize(iterations=100):
    print(f"Starting SPSA Tuning with Fastchess for {iterations} iterations...")
    
    for k in range(1, iterations + 1):
        ck = c / (k + A)**gamma
        ak = a / (k + A)**alpha
        
        delta = {}
        theta_plus = {}
        theta_minus = {}
        
        for name, p in PARAMS.items():
            delta[name] = 1 if random.random() < 0.5 else -1
            change = ck * delta[name] * p["step"]
            theta_plus[name] = max(p["min"], min(p["max"], p["val"] + change))
            theta_minus[name] = max(p["min"], min(p["max"], p["val"] - change))

        w, l, d = run_fastchess_match(theta_plus, theta_minus)
        total_games = w + l + d
        
        if total_games == 0:
            print(f"Iter {k}: No games played/parsed. Skipping update.")
            continue

        score_diff = (w - l) / total_games
        print(f"Iter {k}: Score Diff = {score_diff:.4f} (W{w}-L{l}-D{d})")
        
        for name, p in PARAMS.items():
            gradient = score_diff / (2 * ck * delta[name])
            update = ak * gradient * p["step"] * 10
            new_val = p["val"] + update
            p["val"] = max(p["min"], min(p["max"], new_val))
            print(f"  {name}: {new_val:.2f}")

    print("\n--- Final Tuned Parameters ---")
    for name, p in PARAMS.items():
        print(f"int {name} = {int(p['val'])};")

if __name__ == "__main__":
    iter_arg = int(sys.argv[1]) if len(sys.argv) > 1 else 50
    spsa_optimize(iterations=iter_arg)