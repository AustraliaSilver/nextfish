import os
import subprocess
import threading
import time
import glob
import shutil

# --- CẤU HÌNH ---
WORKING_DIR = "/kaggle/working"
NEXTFISH_BIN = os.path.join(WORKING_DIR, "nextfish/src/stockfish")
MODEL_PATH = "/kaggle/working/model.onnx"
ONNX_DIR = os.path.join(WORKING_DIR, "onnxruntime-linux-x64-gpu-1.17.1")
ONNX_LIB = os.path.join(ONNX_DIR, "lib")
BOOK_PATH = os.environ.get(
    "NEXTFISH_BOOK",
    os.path.join(WORKING_DIR, "nextfish", "UHO_2022_8mvs_+110_+119.pgn"),
)
FALLBACK_BOOK = os.path.join(WORKING_DIR, "openings_fallback.pgn")
CMD_TIMEOUT = 3600

# Link tải công cụ
FASTCHESS_URL = "https://github.com/Disservin/fastchess/releases/download/v1.7.0-alpha/fastchess-linux-x86-64.tar"
STOCKFISH_BASE_URL = "https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-ubuntu-x86-64-avx2.tar"

FALLBACK_POSITIONS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2",
    "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2",
    "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 1",
    "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 0 1",
]


def run_cmd(cmd, desc, cwd=None, timeout=CMD_TIMEOUT):
    print(f"\n[🚀] {desc}...")
    if "wget " in cmd:
        cmd = cmd.replace("wget ", "wget -L -q ")
    process = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=cwd)
    timer = threading.Timer(timeout, process.kill)
    timer.start()
    try:
        for line in process.stdout:
            print(f"  {line.strip()}")
        process.wait()
    finally:
        timer.cancel()
    if process.returncode != 0:
        print(f"[❌] {desc} thất bại (return code {process.returncode})")
        return False
    return True


def find_and_move(pattern, target_name):
    """Tìm file khớp với pattern và di chuyển về WORKING_DIR với tên mới"""
    target = os.path.join(WORKING_DIR, target_name)
    if os.path.isfile(target):
        print(f"[📍] {target_name} đã tồn tại, giữ nguyên.")
        return True
    for f in glob.glob(os.path.join(WORKING_DIR, "**", pattern), recursive=True):
        if os.path.isfile(f) and os.path.abspath(f) != os.path.abspath(target):
            print(f"[📍] Tìm thấy {f}, đang cấu hình thành {target_name}...")
            shutil.copy2(f, target)
            os.chmod(target, 0o755)
            return True
    return False


def ensure_book():
    """Dùng sách mở đầu của repo nếu có, nếu không thì tạo một bộ mở đầu đơn giản."""
    if os.path.isfile(BOOK_PATH):
        print(f"[📖] Dùng sách mở đầu: {BOOK_PATH}")
        return BOOK_PATH
    print(f"[⚠️] Không tìm thấy sách mở đầu tại {BOOK_PATH}, tạo bộ mở đầu dự phòng.")
    with open(FALLBACK_BOOK, "w") as f:
        for fen in FALLBACK_POSITIONS:
            f.write('[Event "Opening"]\n[SetUp "1"]\n')
            f.write(f'[FEN "{fen}"]\n\n')
    print(f"[📖] Đã tạo sách mở đầu dự phòng: {FALLBACK_BOOK}")
    return FALLBACK_BOOK


def setup_chess_env():
    os.makedirs(WORKING_DIR, exist_ok=True)
    os.chdir(WORKING_DIR)

    # 1. Cài đặt Fastchess
    if not os.path.exists("fastchess"):
        if not run_cmd(f"wget {FASTCHESS_URL} -O fastchess.tar", "Tải Fastchess"):
            return False
        if not run_cmd("tar -xf fastchess.tar", "Giải nén Fastchess"):
            return False
        if not find_and_move("fastchess", "fastchess"):
            print("[❌] LỖI: Không tìm thấy binary fastchess sau khi giải nén!")
            return False

    # 2. Cài đặt Stockfish đối thủ (Standard)
    if not os.path.exists("stockfish_base"):
        if not run_cmd(f"wget {STOCKFISH_BASE_URL} -O sf_base.tar", "Tải Stockfish đối thủ"):
            return False
        if not run_cmd("tar -xf sf_base.tar", "Giải nén Stockfish đối thủ"):
            return False
        if not find_and_move("stockfish-ubuntu*", "stockfish_base"):
            if not find_and_move("stockfish*", "stockfish_base"):
                print("[❌] LỖI: Không tìm thấy binary stockfish đối thủ!")
                return False
    return True


def start_tournament():
    print("\n" + "="*60)
    print("⚔️  BẮT ĐẦU GIẢI ĐẤU: NEXTFISH (GPU) VS STOCKFISH STANDARD")
    print("="*60)

    os.chdir(WORKING_DIR)
    # Đường dẫn thư viện ONNX GPU cực kỳ quan trọng cho T4
    if not os.path.isdir(ONNX_LIB):
        print(f"[❌] LỖI: Không tìm thấy thư viện ONNX GPU tại {ONNX_LIB}.")
        return False

    if not os.path.exists(NEXTFISH_BIN):
        print(f"[❌] LỖI: Không tìm thấy Nextfish tại {NEXTFISH_BIN}.")
        return False

    if not os.path.exists("./fastchess") or not os.path.exists("./stockfish_base"):
        print("[❌] LỖI: Thiếu công cụ thi đấu.")
        return False

    if not os.path.exists(MODEL_PATH):
        print(f"[❌] LỖI: Không tìm thấy model tại {MODEL_PATH}.")
        return False

    book = ensure_book()

    # Lệnh Fastchess với LD_LIBRARY_PATH để kích hoạt GPU
    # concurrency 1: Dành toàn bộ GPU cho 1 ván duy nhất để đạt sức mạnh tối đa
    cmd = (
        f"LD_LIBRARY_PATH={ONNX_LIB}:$LD_LIBRARY_PATH ./fastchess "
        f"-engine cmd={NEXTFISH_BIN} name=Nextfish "
        f"option.Lc0Policy_ModelPath={MODEL_PATH} "
        f"option.Lc0Policy_Active=true "
        f"-engine cmd=./stockfish_base name=Stockfish_Standard "
        f"-each tc=10+0.1 "
        f"-rounds 25 -repeat "
        f"-concurrency 1 "
        f"-openings file={book} format=pgn order=random "
        f"-draw movenumber=40 movecount=8 score=8 "
        f"-resign movecount=3 score=600 "
        f"-output pgn=nextfish_battle_report.pgn "
        f"-log file=fastchess.log"
    )

    return run_cmd(cmd, "Đang thi đấu (50 ván)")


if __name__ == "__main__":
    if setup_chess_env():
        if not start_tournament():
            print("[❌] Giải đấu thất bại hoặc bị giới hạn thời gian.")
    else:
        print("[❌] Thiết lập môi trường thất bại. Dừng.")
