import os
import subprocess
import time
import glob
import shutil

# --- CẤU HÌNH ---
WORKING_DIR = "/kaggle/working"
NEXTFISH_BIN = os.path.join(WORKING_DIR, "nextfish/src/stockfish")
MODEL_PATH = os.path.join(WORKING_DIR, "model.onnx")

# Link tải công cụ
FASTCHESS_URL = "https://github.com/Disservin/fastchess/releases/download/v1.7.0-alpha/fastchess-linux-x86-64.tar"
STOCKFISH_BASE_URL = "https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-ubuntu-x86-64-avx2.tar"

def run_cmd(cmd, desc):
    print(f"\n[🚀] {desc}...")
    if "wget " in cmd:
        cmd = cmd.replace("wget ", "wget -L -q ")
    process = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in process.stdout:
        print(f"  {line.strip()}")
    process.wait()
    return process.returncode == 0

def find_and_move(pattern, target_name):
    """Tìm file khớp với pattern và di chuyển về WORKING_DIR với tên mới"""
    files = glob.glob(os.path.join(WORKING_DIR, "**", pattern), recursive=True)
    for f in files:
        if os.path.isfile(f) and f != os.path.join(WORKING_DIR, target_name):
            print(f"[📍] Tìm thấy {f}, đang cấu hình thành {target_name}...")
            shutil.copy2(f, os.path.join(WORKING_DIR, target_name))
            os.chmod(os.path.join(WORKING_DIR, target_name), 0o755)
            return True
    return False

def setup_chess_env():
    os.chdir(WORKING_DIR)
    
    # 1. Cài đặt Fastchess
    if not os.path.exists("fastchess"):
        run_cmd(f"wget {FASTCHESS_URL} -O fastchess.tar", "Tải Fastchess")
        run_cmd("tar -xf fastchess.tar", "Giải nén Fastchess")
        if not find_and_move("fastchess", "fastchess"):
            print("[⚠️] Cảnh báo: Không tìm thấy binary fastchess sau khi giải nén!")
    
    # 2. Cài đặt Stockfish đối thủ (Standard)
    if not os.path.exists("stockfish_base"):
        run_cmd(f"wget {STOCKFISH_BASE_URL} -O sf_base.tar", "Tải Stockfish đối thủ")
        run_cmd("tar -xf sf_base.tar", "Giải nén Stockfish đối thủ")
        # Tìm file bắt đầu bằng stockfish-ubuntu...
        if not find_and_move("stockfish-ubuntu*", "stockfish_base"):
            # Thử tìm file bất kỳ bắt đầu bằng stockfish
            find_and_move("stockfish*", "stockfish_base")

def start_tournament():
    print("\n" + "="*60)
    print("⚔️  BẮT ĐẦU GIẢI ĐẤU: NEXTFISH VS STOCKFISH STANDARD")
    print("="*60)
    
    os.chdir(WORKING_DIR)
    
    if not os.path.exists(NEXTFISH_BIN):
        print(f"[❌] LỖI: Không tìm thấy Nextfish tại {NEXTFISH_BIN}. Hãy build engine trước!")
        return
    
    if not os.path.exists("./fastchess") or not os.path.exists("./stockfish_base"):
        print("[❌] LỖI: Thiếu công cụ thi đấu (fastchess hoặc stockfish_base).")
        return

    # Lệnh Fastchess
    cmd = (
        f"./fastchess "
        f"-engine cmd={NEXTFISH_BIN} name=Nextfish "
        f"option.Lc0Policy_ModelPath={MODEL_PATH} "
        f"option.Lc0Policy_Active=true "
        f"-engine cmd=./stockfish_base name=Stockfish_Standard "
        f"-each tc=10+0.1 "
        f"-rounds 25 -repeat "
        f"-concurrency 2 "
        f"-draw movenumber=40 movecount=8 score=8 "
        f"-resign movecount=3 score=600 "
        f"-output pgn=nextfish_battle_report.pgn "
        f"-log file=fastchess.log"
    )
    
    run_cmd(cmd, "Đang thi đấu (50 ván)")

if __name__ == "__main__":
    setup_chess_env()
    start_tournament()
