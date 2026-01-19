import json
import os
import subprocess
import re
import random

# Cấu hình đường dẫn
SRC_DIR = "src"
STATE_FILE = "src/ideas/tuning_state.json"
ENGINE_PATH = "./src/nextfish" # Tên file build trên Linux
BASE_ENGINE = "./stockfish_baseline"
FASTCHESS = "./fastchess"

def load_state():
    with open(STATE_FILE, 'r') as f:
        return json.load(f)

def save_state(state):
    # Đảm bảo thư mục tồn tại
    os.makedirs(os.path.dirname(STATE_FILE), exist_ok=True)
    with open(STATE_FILE, 'w') as f:
        json.dump(state, f, indent=4)

def update_source_code(params):
    search_cpp = os.path.join(SRC_DIR, "search.cpp")
    with open(search_cpp, 'r') as f:
        content = f.read()

    # Cập nhật Time Multipliers
    content = re.sub(r"whiteTimeMultiplier = \(us == WHITE\) \? [\d\.]+ : 1.0;", 
                     f"whiteTimeMultiplier = (us == WHITE) ? {params['whiteTimeMultiplier']:.2f} : 1.0;", content)
    content = re.sub(r"blackTimeMultiplier = \(us == BLACK && bestValue < -10 && bestValue > -100\) \? [\d\.]+ : 1.0;", 
                     f"blackTimeMultiplier = (us == BLACK && bestValue < -10 && bestValue > -100) ? {params['blackTimeMultiplier']:.2f} : 1.0;", content)
    
    # Cập nhật Singularity & LMR
    content = re.sub(r"singularityCore = \(us == WHITE\) \? \d+ : 628;", 
                     f"singularityCore = (us == WHITE) ? {params['singularityCore']} : 628;", content)
    content = re.sub(r"baseOffset = \(us == WHITE\) \? \d+ : 1182;", 
                     f"baseOffset = (us == WHITE) ? {params['baseOffset']} : 1182;", content)

    # Cập nhật Optimism & Contempt
    content = re.sub(r"int optimismBase = \(us == WHITE\) \? \d+ : 142;", 
                     f"int optimismBase = (us == WHITE) ? {params['optimismBase']} : 142;", content)
    content = re.sub(r"if \(pos\.side_to_move\(\) == WHITE\) v \+= \d+;", 
                     f"if (pos.side_to_move() == WHITE) v += {params['contempt']};", content)
    content = re.sub(r"else if \(pos\.side_to_move\(\) == BLACK\) v -= \d+;", 
                     f"else if (pos.side_to_move() == BLACK) v -= {params['contempt']};", content)

    with open(search_cpp, 'w') as f:
        f.write(content)

def build_engine():
    print("--- Biên dịch Nextfish ---")
    os.chdir(SRC_DIR)
    # Lệnh biên dịch cho Linux
    cmd = "g++ -O3 -std=c++17 -DNDEBUG -DIS_64BIT -march=native *.cpp syzygy/tbprobe.cpp nnue/*.cpp nnue/features/*.cpp -o nextfish -lpthread"
    subprocess.run(cmd, shell=True, check=True)
    os.chdir("../")

def run_match():
    print("--- Chạy match kiểm chứng với fastchess ---")
    # Cấu hình book (Giả sử file UHO đã được push lên cùng source)
    book_path = "UHO_2022_8mvs_+110_+119.pgn"
    
    # Lệnh fastchess: thêm -openings, sửa format
    cmd = f"{FASTCHESS} -engine cmd={ENGINE_PATH} name=Nextfish -engine cmd={BASE_ENGINE} name=Baseline -each tc=10+0.1 -rounds 10 -concurrency 2 -openings file={book_path} format=pgn order=random"
    
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    
    # In ra log để debug (dùng repr để tránh lỗi shell interpret ký tự lạ)
    print(result.stdout)
    
    # Regex mới: Bắt được cả "Elo: 12.3" và "Elo : 12.3"
    match = re.search(r"Elo\s*:\s*([\d\.-]+)", result.stdout)
    if match:
        return float(match.group(1))
    
    # Fallback: Nếu không tìm thấy Elo, thử tìm "Points: X (Y %)" để tính toán
    match_pts = re.search(r"Points:\s*[\d\.]+\s*\(([\d\.]+) %\)", result.stdout)
    if match_pts:
        win_rate = float(match_pts.group(1))
        # Công thức quy đổi Winrate sang Elo đơn giản: Elo = -400 * log10(1/winrate - 1)
        # Ở đây dùng logic đơn giản cho nhanh
        return (win_rate - 50.0) * 4 
        
    return -999

def tune():
    state = load_state()
    state['iteration'] += 1
    
    print(f"Tuning Iteration: {state['iteration']}/{state['max_iterations']}")
    
    # Lưu lại params cũ để so sánh
    old_params = state['parameters'].copy()
    
    # Áp dụng biến thiên ngẫu nhiên cho TOÀN BỘ tham số
    params = state['parameters']
    
    # Định nghĩa các bước nhảy (step size) cho từng tham số
    steps = {
        'whiteTimeMultiplier': 0.02,
        'blackTimeMultiplier': 0.02,
        'singularityCore': 2,
        'baseOffset': 1,
        'optimismBase': 2,
        'contempt': 1
    }

    # Mỗi lần lặp sẽ chọn ngẫu nhiên 2-3 tham số để thay đổi (Tránh đổi quá nhiều gây nhiễu)
    to_tweak = random.sample(list(steps.keys()), k=random.randint(2, 4))
    
    print(f"Tweaking parameters: {to_tweak}")
    for p in to_tweak:
        params[p] += random.choice([-steps[p], steps[p]])

    # Ràng buộc giá trị hợp lý (Safety Bounds)
    params['whiteTimeMultiplier'] = max(1.1, min(1.8, params['whiteTimeMultiplier']))
    params['blackTimeMultiplier'] = max(1.1, min(1.8, params['blackTimeMultiplier']))
    params['singularityCore'] = max(600, min(700, params['singularityCore']))
    params['baseOffset'] = max(1150, min(1200, params['baseOffset']))
    
    update_source_code(params)
    try:
        build_engine()
        elo = run_match()
        print(f"Result Elo: {elo}")
        
        # Nếu Elo quá tệ, quay lại params cũ
        if elo < -50:
            state['parameters'] = old_params
            print("Kết quả tệ, hoàn tác tham số.")
    except Exception as e:
        print(f"Lỗi trong quá trình build/match: {e}")
        state['parameters'] = old_params

    save_state(state)
    return state['iteration'] >= state['max_iterations']

if __name__ == "__main__":
    is_finished = tune()
    if is_finished:
        print("Đã đạt giới hạn 15 lần tuning. Sẵn sàng push code mới.")
        # Thoát với mã lỗi đặc biệt để workflow biết cần push code
        exit(100)
    else:
        exit(0)
