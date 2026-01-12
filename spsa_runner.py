import subprocess
import random
import sys
import re
import json
import os

# --- CẤU HÌNH TUNING ---
# Định nghĩa các tham số cần tune và giá trị khởi điểm hiện tại
# Bạn có thể thêm bất kỳ biến "double" nào trong nextfish_strategy.cpp vào đây
PARAMS = {
    "WhiteOptimism":       {"val": 20.85, "min": 0.0,   "max": 40.0,  "step": 1.5},
    "BlackLossPessimism":  {"val": -16.77, "min": -40.0, "max": 0.0,   "step": 1.5},
    "VolatilityThreshold": {"val": 13.83, "min": 5.0,   "max": 30.0,  "step": 0.8},
    "CodeRedLMR":          {"val": 63.31, "min": 40.0,  "max": 95.0,  "step": 1.5},
    "BlackLMR":            {"val": 87.90, "min": 70.0,  "max": 100.0, "step": 1.5},
    # Thêm các tham số mới chưa được tune ở đây nếu muốn
    "WhiteAggression":     {"val": 25.0,  "min": 10.0,  "max": 50.0,  "step": 2.0}, 
    "PanicTimeFactor":     {"val": 2.0,   "min": 1.1,   "max": 4.0,   "step": 0.2},
}

# SPSA Constants
c = 2.0
A = 100
alpha = 0.602
gamma = 0.101

# Games per iteration (Total games = iterations * games_per_iter)
GAMES_PER_ITER = 20 

def run_fastchess_match(params_A, params_B):
    """Chạy match giữa 2 phiên bản params"""
    
    # Tạo chuỗi option UCI (Nextfish dùng double nên gửi số thực)
    opts_A = " ".join([f"option.{k}={v:.4f}" for k, v in params_A.items()])
    opts_B = " ".join([f"option.{k}={v:.4f}" for k, v in params_B.items()])

    cmd = [
        "./fastchess",
        "-engine", "name=Tuned", "cmd=./nextfish", opts_A,
        "-engine", "name=Base",  "cmd=./nextfish", opts_B,
        "-each", "tc=10+0.1", "option.Hash=8", "option.Threads=1", "proto=uci",
        "-rounds", str(GAMES_PER_ITER // 2), 
        "-games", "2",
        "-repeat",
        "-concurrency", "2",
        "-recover",
        "-openings", "file=UHO_2022_8mvs_+110_+119.pgn", "format=pgn", "order=random",
        "-pgnout", "file=spsa_chunk.pgn"
    ]
    
    print(f"Running {GAMES_PER_ITER} games...")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        output = result.stdout + result.stderr
        
        # Đếm kết quả thủ công để chính xác
        w = len(re.findall(r"Tuned vs Base"): 1-0", output)) + \
            len(re.findall(r"Base vs Tuned"): 0-1", output))
            
        l = len(re.findall(r"Tuned vs Base"): 0-1", output)) + \
            len(re.findall(r"Base vs Tuned"): 1-0", output))
            
        d = len(re.findall(r": 1/2-1/2", output))
        
        return w, l, d
    except Exception as e:
        print(f"Error: {e}")
        return 0, 0, 0

def spsa_optimize(iterations=15):
    print(f"--- STARTING SPSA AUTO-TUNING ({iterations} iterations) ---")
    
    for k in range(1, iterations + 1):
        ck = c / (k + A)**gamma
        ak = 1.5 / (k + A)**alpha # Learning rate
        
        delta = {}
        theta_plus = {}
        theta_minus = {}
        
        # 1. Tạo nhiễu (Perturbation)
        for name, p in PARAMS.items():
            delta[name] = 1 if random.random() < 0.5 else -1
            change = ck * delta[name] * p["step"]
            
            theta_plus[name]  = max(p["min"], min(p["max"], p["val"] + change))
            theta_minus[name] = max(p["min"], min(p["max"], p["val"] - change))

        # 2. Đấu: Tuned(+) vs Base(-)
        w, l, d = run_fastchess_match(theta_plus, theta_minus)
        total = w + l + d
        if total == 0: continue

        score_diff = (w - l) / total
        print(f"Iter {k}/{iterations}: Score Diff = {score_diff:.4f} (+{w} -{l} ={d})")
        
        # 3. Cập nhật tham số (Gradient Descent)
        for name, p in PARAMS.items():
            gradient = score_diff / (2 * ck * delta[name])
            update = ak * gradient * p["step"] * 20.0 # Scale factor
            
            old_val = p["val"]
            new_val = p["val"] + update
            p["val"] = max(p["min"], min(p["max"], new_val))
            
            # Log thay đổi lớn
            if abs(new_val - old_val) > 0.1:
                print(f"  > {name}: {old_val:.2f} -> {new_val:.2f}")

    # --- KẾT THÚC: LƯU KẾT QUẢ ---
    final_results = {k: v["val"] for k, v in PARAMS.items()}
    
    print("\n--- FINAL PARAMETERS ---")
    print(json.dumps(final_results, indent=2))
    
    with open("tuned_params.json", "w") as f:
        json.dump(final_results, f)

if __name__ == "__main__":
    # Đọc tham số từ file C++ hiện tại để làm mốc bắt đầu (nếu cần thiết)
    # Ở đây ta dùng giá trị khởi tạo trong PARAMS cho đơn giản
    
    iters = int(sys.argv[1]) if len(sys.argv) > 1 else 15
    spsa_optimize(iterations=iters)
