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
    # Khởi tạo mặc định nếu file chưa tồn tại hoặc thiếu trường
    default_state = {
        "iteration": 0,
        "max_iterations": 15,
        "parameters": {
            "whiteTimeMultiplier": 1.40,
            "blackTimeMultiplier": 1.40,
            "complexityMultiplier": 1.20,
            "singularityCore": 640,
            "baseOffset": 1175,
            "optimismBase": 176,
            "contempt": 10
        }
    }
    if not os.path.exists(STATE_FILE):
        return default_state
    
    with open(STATE_FILE, 'r') as f:
        state = json.load(f)
        # Bổ sung complexityMultiplier nếu chưa có
        if "complexityMultiplier" not in state["parameters"]:
            state["parameters"]["complexityMultiplier"] = 1.20
        return state

def save_state(state):
    os.makedirs(os.path.dirname(STATE_FILE), exist_ok=True)
    with open(STATE_FILE, 'w') as f:
        json.dump(state, f, indent=4)

def update_source_code(params):
    search_cpp = os.path.join(SRC_DIR, "search.cpp")
    with open(search_cpp, 'r') as f:
        content = f.read()

    # Cập nhật Time Multipliers (Làm tròn 2 chữ số)
    content = re.sub(r"whiteTimeMultiplier = \(us == WHITE\) \? [\d\.]+ : 1.0;", 
                     f"whiteTimeMultiplier = (us == WHITE) ? {round(params['whiteTimeMultiplier'], 2)} : 1.0;", content)
    content = re.sub(r"blackTimeMultiplier = \(us == BLACK && bestValue < -10 && bestValue > -100\) \? [\d\.]+ : 1.0;", 
                     f"blackTimeMultiplier = (us == BLACK && bestValue < -10 && bestValue > -100) ? {round(params['blackTimeMultiplier'], 2)} : 1.0;", content)
    
    # Cập nhật Complexity Multiplier (Phase 124)
    content = re.sub(r"complexityMultiplier = \(mainThread->completedDepth >= 10 &&.*?\) \? [\d\.]+ : 1.0;", 
                     f"complexityMultiplier = (mainThread->completedDepth >= 10 && (mainThread->rootMoves[0].pv[0] != prevBestMove || std::abs(avg - prevScore) > 20)) ? {round(params['complexityMultiplier'], 2)} : 1.0;", content, flags=re.DOTALL)

    # Cập nhật Singularity & LMR (Số nguyên)
    content = re.sub(r"singularityCore = \(us == WHITE\) \? \d+ : 628;", 
                     f"singularityCore = (us == WHITE) ? {int(params['singularityCore'])} : 628;", content)
    content = re.sub(r"baseOffset = \(us == WHITE\) \? \d+ : 1182;", 
                     f"baseOffset = (us == WHITE) ? {int(params['baseOffset'])} : 1182;", content)

    # Cập nhật Optimism & Contempt
    content = re.sub(r"int optimismBase = \(us == WHITE\) \? \d+ : 142;", 
                     f"int optimismBase = (us == WHITE) ? {int(params['optimismBase'])} : 142;", content)
    content = re.sub(r"if \(pos\.side_to_move\(\) == WHITE\) v \+= \d+;", 
                     f"if (pos.side_to_move() == WHITE) v += {int(params['contempt'])};", content)
    content = re.sub(r"else if \(pos\.side_to_move\(\) == BLACK\) v -= \d+;", 
                     f"else if (pos.side_to_move() == BLACK) v -= {int(params['contempt'])};", content)

    with open(search_cpp, 'w') as f:
        f.write(content)

def build_engine():
    print("--- Biên dịch Nextfish ---")
    os.chdir(SRC_DIR)
    cmd = "g++ -O3 -std=c++17 -DNDEBUG -DIS_64BIT -march=native *.cpp syzygy/tbprobe.cpp nnue/*.cpp nnue/features/*.cpp -o nextfish -lpthread"
    subprocess.run(cmd, shell=True, check=True)
    os.chdir("../")

def run_match():
    print("--- Chạy match kiểm chứng với fastchess ---")
    book_path = "UHO_2022_8mvs_+110_+119.pgn"
    cmd = f"./fastchess -engine cmd={ENGINE_PATH} name=Nextfish -engine cmd={BASE_ENGINE} name=Baseline -each tc=10+0.1 -rounds 10 -concurrency 2 -openings file={book_path} format=pgn order=random"
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    print(result.stdout)
    match = re.search(r"Elo\s*:\s*([\d\.-]+)", result.stdout)
    if match:
        return float(match.group(1))
    match_pts = re.search(r"Points:\s*[\d\.]+\s*\(([\d\.]+) %\)", result.stdout)
    if match_pts:
        return (float(match_pts.group(1)) - 50.0) * 4 
    return -999

def tune():
    state = load_state()
    state['iteration'] += 1
    print(f"Tuning Iteration: {state['iteration']}/{state['max_iterations']}")
    old_params = state['parameters'].copy()
    params = state['parameters']
    
    steps = {
        'whiteTimeMultiplier': 0.02,
        'blackTimeMultiplier': 0.02,
        'complexityMultiplier': 0.05,
        'singularityCore': 2,
        'baseOffset': 1,
        'optimismBase': 2,
        'contempt': 1
    }

    to_tweak = random.sample(list(steps.keys()), k=random.randint(2, 4))
    print(f"Tweaking parameters: {to_tweak}")
    for p in to_tweak:
        params[p] += random.choice([-steps[p], steps[p]])

    params['whiteTimeMultiplier'] = max(1.1, min(1.8, params['whiteTimeMultiplier']))
    params['blackTimeMultiplier'] = max(1.1, min(1.8, params['blackTimeMultiplier']))
    params['complexityMultiplier'] = max(1.0, min(1.5, params['complexityMultiplier']))
    params['singularityCore'] = max(600, min(700, params['singularityCore']))
    params['baseOffset'] = max(1150, min(1200, params['baseOffset']))
    
    update_source_code(params)
    try:
        build_engine()
        elo = run_match()
        print(f"Result Elo: {elo}")
        if elo < -50:
            state['parameters'] = old_params
            print("Kết quả tệ, hoàn tác tham số.")
    except Exception as e:
        print(f"Lỗi: {e}")
        state['parameters'] = old_params

    save_state(state)
    return state['iteration'] >= state['max_iterations']

if __name__ == "__main__":
    is_finished = tune()
    if is_finished:
        print("Done 15 iterations.")
        exit(100)
    else:
        exit(0)