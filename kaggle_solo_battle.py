import os
import subprocess
import time

# --- CẤU HÌNH ---
WORKING_DIR = "/kaggle/working"
NEXTFISH_BIN = os.path.join(WORKING_DIR, "nextfish/src/stockfish")
MODEL_PATH = os.path.join(WORKING_DIR, "model.onnx")

# Link tải công cụ
FASTCHESS_URL = "https://github.com/FastChess/fastchess/releases/download/v0.9.0/fastchess-v0.9.0-linux-x86-64.tar.gz"
STOCKFISH_BASE_URL = "https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-ubuntu-x86-64-avx2.tar.gz"

def run_cmd(cmd, desc):
    print(f"\n[🚀] {desc}...")
    process = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in process.stdout:
        print(f"  {line.strip()}")
    process.wait()
    return process.returncode == 0

def setup_chess_env():
    os.chdir(WORKING_DIR)
    
    # 1. Cài đặt Fastchess
    if not os.path.exists("fastchess"):
        run_cmd(f"wget {FASTCHESS_URL} -O fastchess.tar.gz && tar -zxvf fastchess.tar.gz && chmod +x fastchess", "Tải Fastchess")
    
    # 2. Cài đặt Stockfish đối thủ (Standard)
    if not os.path.exists("stockfish_base"):
        run_cmd(f"wget {STOCKFISH_BASE_URL} -O sf_base.tar.gz && tar -zxvf sf_base.tar.gz", "Tải Stockfish đối thủ")
        # Tìm file thực thi và đưa ra ngoài
        run_cmd(f"find . -name 'stockfish-ubuntu-x86-64-avx2' -exec mv {{}} {WORKING_DIR}/stockfish_base \\;", "Cấu hình Stockfish_Standard")
        run_cmd(f"chmod +x {WORKING_DIR}/stockfish_base", "Cấp quyền thực thi")

def start_tournament():
    print("\n" + "="*60)
    print("⚔️  BẮT ĐẦU GIẢI ĐẤU: NEXTFISH VS STOCKFISH STANDARD")
    print("="*60)
    
    if not os.path.exists(NEXTFISH_BIN):
        print(f"[❌] LỖI: Không tìm thấy Nextfish tại {NEXTFISH_BIN}. Hãy build engine trước!")
        return

    # Lệnh Fastchess: 50 ván (25 rounds * repeat), 2 ván chạy song song
    # Time Control: 10 phút + 0.1s cộng thêm mỗi nước
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
        f"-pgn nextfish_battle_report.pgn"
    )
    
    run_cmd(cmd, "Đang thi đấu (50 ván)")

if __name__ == "__main__":
    setup_chess_env()
    start_tournament()
