import glob
import json
import math
import os
import re
import shutil
import subprocess
import sys
import random

# Cấu hình đường dẫn
SRC_DIR = "src"
STATE_FILE = "src/ideas/tuning_state.json"
BACKUP_FILE = "src/ideas/search.cpp.autotune_backup"
ENGINE_PATH = "./src/nextfish" # Tên file build trên Linux
BASE_ENGINE = "./stockfish_baseline"
FASTCHESS = "./fastchess"

# Ngưỡng chấp nhận: Elo > 0 và LOS >= 95%
MIN_LOS = 95.0

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
        try:
            state = json.load(f)
        except json.JSONDecodeError as e:
            raise RuntimeError(f"File trạng thái bị lỗi: {STATE_FILE}: {e}") from e

    if not isinstance(state, dict) or not isinstance(state.get("parameters"), dict):
        raise RuntimeError(f"File trạng thái bị lỗi: {STATE_FILE} thiếu trường 'parameters'")

    # Bổ sung complexityMultiplier nếu chưa có
    if "complexityMultiplier" not in state["parameters"]:
        state["parameters"]["complexityMultiplier"] = 1.20
    return state

def save_state(state):
    directory = os.path.dirname(STATE_FILE)
    if directory:
        os.makedirs(directory, exist_ok=True)
    tmp_file = STATE_FILE + ".tmp"
    with open(tmp_file, 'w') as f:
        json.dump(state, f, indent=4)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp_file, STATE_FILE)

def backup_source():
    search_cpp = os.path.join(SRC_DIR, "search.cpp")
    os.makedirs(os.path.dirname(BACKUP_FILE), exist_ok=True)
    shutil.copy2(search_cpp, BACKUP_FILE)

def restore_source():
    search_cpp = os.path.join(SRC_DIR, "search.cpp")
    if os.path.exists(BACKUP_FILE):
        shutil.copy2(BACKUP_FILE, search_cpp)
        os.remove(BACKUP_FILE)

def update_source_code(params):
    search_cpp = os.path.join(SRC_DIR, "search.cpp")
    with open(search_cpp, 'r') as f:
        content = f.read()
    original = content

    substitutions = [
        (r"whiteTimeMultiplier = \(us == WHITE\) \? [\d\.]+ : 1.0;",
         f"whiteTimeMultiplier = (us == WHITE) ? {round(params['whiteTimeMultiplier'], 2)} : 1.0;", 0),
        (r"blackTimeMultiplier = \(us == BLACK && bestValue < -10 && bestValue > -100\) \? [\d\.]+ : 1.0;",
         f"blackTimeMultiplier = (us == BLACK && bestValue < -10 && bestValue > -100) ? {round(params['blackTimeMultiplier'], 2)} : 1.0;", 0),
        (r"complexityMultiplier = \(mainThread->completedDepth >= 10 &&.*?\) \? [\d\.]+ : 1.0;",
         f"complexityMultiplier = (mainThread->completedDepth >= 10 && (mainThread->rootMoves[0].pv[0] != prevBestMove || std::abs(avg - prevScore) > 20)) ? {round(params['complexityMultiplier'], 2)} : 1.0;", re.DOTALL),
        (r"singularityCore = \(us == WHITE\) \? \d+ : 628;",
         f"singularityCore = (us == WHITE) ? {int(params['singularityCore'])} : 628;", 0),
        (r"baseOffset = \(us == WHITE\) \? \d+ : 1182;",
         f"baseOffset = (us == WHITE) ? {int(params['baseOffset'])} : 1182;", 0),
        (r"int optimismBase = \(us == WHITE\) \? \d+ : 142;",
         f"int optimismBase = (us == WHITE) ? {int(params['optimismBase'])} : 142;", 0),
        (r"if \(pos\.side_to_move\(\) == WHITE\) v \+= \d+;",
         f"if (pos.side_to_move() == WHITE) v += {int(params['contempt'])};", 0),
        (r"else if \(pos\.side_to_move\(\) == BLACK\) v -= \d+;",
         f"else if (pos.side_to_move() == BLACK) v -= {int(params['contempt'])};", 0),
    ]

    for pattern, replacement, flags in substitutions:
        new_content, count = re.subn(pattern, replacement, content, flags=flags)
        if count == 0:
            raise RuntimeError(f"Không tìm thấy mẫu trong search.cpp: {pattern}")
        content = new_content

    if content == original:
        raise RuntimeError("Không có thay đổi nào được áp dụng cho search.cpp")

    with open(search_cpp, 'w') as f:
        f.write(content)

def build_engine():
    print("--- Biên dịch Nextfish ---")
    sources = sorted(glob.glob(os.path.join(SRC_DIR, "*.cpp")))
    sources += sorted(glob.glob(os.path.join(SRC_DIR, "syzygy", "*.cpp")))
    sources += sorted(glob.glob(os.path.join(SRC_DIR, "nnue", "*.cpp")))
    sources += sorted(glob.glob(os.path.join(SRC_DIR, "nnue", "features", "*.cpp")))
    cmd = ["g++", "-O3", "-std=c++17", "-DNDEBUG", "-DIS_64BIT", "-march=native"] + sources + \
          ["-o", os.path.join(SRC_DIR, "nextfish"), "-lpthread"]
    subprocess.run(cmd, cwd=SRC_DIR, check=True)

def score_to_elo(score):
    if score <= 0.0:
        return -1000.0
    if score >= 1.0:
        return 1000.0
    return -400.0 * math.log10((1.0 / score) - 1.0)

def run_match():
    print("--- Chạy match kiểm chứng với fastchess ---")
    book_path = "UHO_2022_8mvs_+110_+119.pgn"
    cmd = [FASTCHESS, "-engine", f"cmd={ENGINE_PATH}", "name=Nextfish",
           "-engine", f"cmd={BASE_ENGINE}", "name=Baseline",
           "-each", "tc=10+0.1", "-rounds", "10", "-concurrency", "2",
           "-openings", f"file={book_path}", "format=pgn", "order=random"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    print(result.stdout)
    output = result.stdout or ""

    los = float("nan")
    los_match = re.search(r"LOS:\s*([0-9]*\.?[0-9]+)\s*%", output)
    if los_match:
        los = float(los_match.group(1))

    elo_match = re.search(r"Elo difference:\s*([-+]?[0-9]*\.?[0-9]+)", output)
    if elo_match:
        return float(elo_match.group(1)), los

    match_pts = re.search(r"Points:\s*[\d\.]+\s*\(([\d\.]+) %\)", output)
    if match_pts:
        return score_to_elo(float(match_pts.group(1)) / 100.0), los
    return -999.0, los

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
    params['optimismBase'] = max(100, min(250, params['optimismBase']))
    params['contempt'] = max(0, min(50, params['contempt']))

    backup_source()
    try:
        update_source_code(params)
        build_engine()
        elo, los = run_match()
        print(f"Result Elo: {elo} (LOS: {los}%)")
        if elo > 0 and los >= MIN_LOS:
            print("Kết quả tốt, giữ tham số.")
        else:
            state['parameters'] = old_params
            restore_source()
            print("Kết quả không đáng tin cậy, hoàn tác tham số.")
    except Exception as e:
        print(f"Lỗi: {e}")
        state['parameters'] = old_params
        restore_source()

    save_state(state)
    return state['iteration'] >= state['max_iterations']

if __name__ == "__main__":
    is_finished = tune()
    if is_finished:
        print("Done 15 iterations.")
        sys.exit(0)
    else:
        sys.exit(0)
