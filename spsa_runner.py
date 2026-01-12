import subprocess
import random
import sys
import math

# Tuning Configuration
PARAMS = {
    "WhiteOptimism":       {"val": 20, "min": 0,   "max": 40,  "step": 2.0},
    "BlackLossPessimism":  {"val": -15, "min": -40, "max": 0,   "step": 2.0},
    "VolatilityThreshold": {"val": 14, "min": 5,   "max": 30,  "step": 1.0},
    "CodeRedLMR":          {"val": 65, "min": 40,  "max": 95,  "step": 2.0},
    "BlackLMR":            {"val": 88, "min": 70,  "max": 100, "step": 2.0},
}

# SPSA Constants
a = 2.0  # Step size scaling
c = 4.0  # Perturbation scaling
A = 100  # Stability constant
alpha = 0.602
gamma = 0.101

def run_match(params_A, params_B, games_per_iter=4):
    """Runs a match between Engine A (params_A) and Engine B (params_B)"""
    
    # Construct UCI options string for Engine A
    opts_A = " ".join([f"option.{k}={int(v)}" for k, v in params_A.items()])
    # Construct UCI options string for Engine B
    opts_B = " ".join([f"option.{k}={int(v)}" for k, v in params_B.items()])

    cmd = [
        "./cutechess-cli",
        "-engine", "name=Nextfish_A", "cmd=./nextfish", "proto=uci", opts_A,
        "-engine", "name=Nextfish_B", "cmd=./nextfish", "proto=uci", opts_B,
        "-each", "tc=1+0.02", "option.Hash=8", "option.Threads=1",
        "-games", "2", "-rounds", str(games_per_iter // 2), "-repeat",
        "-concurrency", "2",
        "-pgnout", "spsa_chunk.pgn"
    ]
    
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    # Parse result
    wins_A = result.stdout.count("Score of Nextfish_A vs Nextfish_B: 1 - 0")
    wins_B = result.stdout.count("Score of Nextfish_A vs Nextfish_B: 0 - 1")
    # Note: cutechess output parsing might need adjustment based on exact output format
    # Simple score extraction:
    import re
    match = re.search(r"Score of Nextfish_A vs Nextfish_B: (\d+) - (\d+) - (\d+)", result.stdout)
    if match:
        w, l, d = map(int, match.groups())
        return w, l, d
    return 0, 0, 0

def spsa_optimize(iterations=100):
    print(f"Starting SPSA Tuning for {iterations} iterations...")
    
    for k in range(1, iterations + 1):
        # 1. Perturb parameters
        ck = c / (k + A)**gamma
        ak = a / (k + A)**alpha
        
        delta = {}
        theta_plus = {}
        theta_minus = {}
        
        for name, p in PARAMS.items():
            # Bernoulli distribution for direction (+1 or -1)
            delta[name] = 1 if random.random() < 0.5 else -1
            
            # Perturb
            change = ck * delta[name] * p["step"]
            theta_plus[name] = max(p["min"], min(p["max"], p["val"] + change))
            theta_minus[name] = max(p["min"], min(p["max"], p["val"] - change))

        # 2. Run match: Theta+ vs Theta-
        w, l, d = run_match(theta_plus, theta_minus)
        score_diff = (w - l) / (w + l + d + 0.001) # Normalized score difference [-1, 1] 

        # 3. Update parameters (Gradient Descent)
        print(f"Iter {k}: Score Diff = {score_diff:.4f} (W{w}-L{l}-D{d})")
        
        for name, p in PARAMS.items():
            gradient = score_diff / (2 * ck * delta[name])
            update = ak * gradient * p["step"] * 10 # Scale up update for integer params
            
            new_val = p["val"] + update
            p["val"] = max(p["min"], min(p["max"], new_val))
            
            print(f"  {name}: {new_val:.2f}")

    print("\n--- Final Tuned Parameters ---")
    for name, p in PARAMS.items():
        print(f"int {name} = {int(p['val'])};")

if __name__ == "__main__":
    spsa_optimize(iterations=int(sys.argv[1]) if len(sys.argv) > 1 else 50)
