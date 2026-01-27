import os
import subprocess
import time

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
        cmd = cmd.replace("wget ", "wget -L ")
    process = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in process.stdout:
        print(f"  {line.strip()}")
    process.wait()
    return process.returncode == 0

def setup_chess_env():
    os.chdir(WORKING_DIR)
    
    # 1. Cài đặt Fastchess
    if not os.path.exists("fastchess"):
        run_cmd(f"wget {FASTCHESS_URL} -O fastchess.tar", "Tải Fastchess")
        run_cmd("tar -xf fastchess.tar", "Giải nén Fastchess")
        run_cmd("find . -name 'fastchess' -type f -exec mv {} ./fastchess \;";, "Định vị Fastchess binary")
        run_cmd("chmod +x fastchess", "Cấp quyền Fastchess")
    
    # 2. Cài đặt Stockfish đối thủ (Standard)
    if not os.path.exists("stockfish_base"):
        run_cmd(f"wget {STOCKFISH_BASE_URL} -O sf_base.tar", "Tải Stockfish đối thủ")
        run_cmd("tar -xf sf_base.tar", "Giải nén Stockfish đối thủ")
        run_cmd(f"find . -name 'stockfish-ubuntu-x86-64-avx2' -type f -exec mv {{}} {WORKING_DIR}/stockfish_base \;";, "Cấu hình Stockfish_Standard")
        run_cmd(f"chmod +x {WORKING_DIR}/stockfish_base", "Cấp quyền thực thi")

def start_tournament():
    print("\n" + "="*60)
    print("⚔️  BẮT ĐẦU GIẢI ĐẤU: NEXTFISH VS STOCKFISH STANDARD")
    print("="*60)
    
    if not os.path.exists(NEXTFISH_BIN):
        print(f"[❌] LỖI: Không tìm thấy Nextfish tại {NEXTFISH_BIN}. Hãy build engine trước!")
        return

    # Lệnh Fastchess: 50 ván, 2 ván chạy song song
    # -output pgn=... là tham số thay thế cho -pgn trong các bản mới
    cmd = (
        f"./fastchess "
        f"-engine cmd={NEXTFISH_BIN} name=Nextfish "
        f"option.Lc0Policy_ModelPath={MODEL_PATH} "
        f"option.Lc0Policy_Active=true "
        f"-engine cmd={WORKING_DIR}/stockfish_base name=Stockfish_Standard "
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
