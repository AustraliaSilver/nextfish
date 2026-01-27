import os
import subprocess
import time
import sys

"""
👑 NEXTFISH V2.0 - KAGGLE MASTER CONTROL SCRIPT
Hệ thống tự động hóa: Cài đặt -> Biên dịch -> Chuyển đổi Model -> Chạy thử Engine Hybrid
Sử dụng cho: Kaggle (GPU T4), Google Colab, hoặc Linux (Ubuntu/Debian)
"""

# --- CẤU HÌNH HỆ THỐNG ---
REPO_URL = "https://github.com/AustraliaSilver/nextfish.git"
MODEL_URL = "https://storage.lczero.org/files/BT4-it332.pb.gz"
ARCH = "x86-64-avx2"  # Kiến trúc tối ưu cho CPU Kaggle

def run_cmd(cmd, desc):
    print(f"\n[🚀] {desc}...")
    try:
        # Sử dụng Popen để stream output trực tiếp
        process = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        for line in process.stdout:
            print(f"  {line.strip()}")
        process.wait()
        if process.returncode == 0:
            print(f"[✅] {desc} thành công.")
        else:
            print(f"[❌] {desc} thất bại.")
    except Exception as e:
        print(f"[❌] Lỗi hệ thống: {e}")

def main():
    start_time = time.time()
    
    # 1. Cài đặt thư viện hệ thống
    run_cmd("apt-get update && apt-get install -y libonnxruntime-dev build-essential wget git", "Cài đặt thư viện C++ & ONNX")
    run_cmd("pip install onnxruntime-gpu tf2onnx", "Cài đặt thư viện Python AI")

    # 2. Xử lý mã nguồn - Clone thẳng vào thư mục hiện tại hoặc thư mục con
    working_dir = "/kaggle/working"
    if not os.path.exists(working_dir):
        working_dir = os.getcwd()

    repo_dir = os.path.join(working_dir, "nextfish")
    
    if os.path.exists(repo_dir):
        run_cmd(f"rm -rf {repo_dir}", "Dọn dẹp thư mục cũ")
    
    run_cmd(f"git clone {REPO_URL} {repo_dir}", "Tải mã nguồn Nextfish")
    
    # Tự động tìm thư mục chứa 'src'
    root_path = repo_dir
    for root, dirs, files in os.walk(repo_dir):
        if "src" in dirs and "evaluate.cpp" in os.listdir(os.path.join(root, "src")):
            root_path = root
            break
    
    print(f"[📍] Thư mục gốc dự án: {root_path}")
    os.chdir(root_path)

    # 3. Xử lý Model Lc0
    print("\n[🧠] Đang chuẩn bị bộ não Lc0 (BT4-it332)...")
    
    # Tự động tìm kiếm trong thư mục input của Kaggle
    kaggle_input_path = "/kaggle/input/neuronnetwork"
    local_model_found = False
    
    if os.path.exists(kaggle_input_path):
        for root, dirs, files in os.walk(kaggle_input_path):
            for file in files:
                if "BT4-it332" in file and (file.endswith(".pb.gz") or file.endswith(".pb")):
                    source_path = os.path.join(root, file)
                    print(f"[📍] Tìm thấy model tại: {source_path}")
                    run_cmd(f"cp '{source_path}' ./BT4-it332.pb.gz", "Sao chép model từ Kaggle Input")
                    local_model_found = True
                    break
            if local_model_found: break

    if not os.path.exists("model.onnx"):
        if not local_model_found and not os.path.exists("BT4-it332.pb.gz"):
            run_cmd(f"wget {MODEL_URL} -O BT4-it332.pb.gz", "Tải Model Lc0 từ storage (do không tìm thấy file cục bộ)")
        
        if os.path.exists("BT4-it332.pb.gz"):
            run_cmd("gunzip -f BT4-it332.pb.gz", "Giải nén Model")
        
        # Nếu file đã giải nén sẵn hoặc vừa giải nén xong
        pb_file = "BT4-it332.pb"
        if not os.path.exists(pb_file):
            # Tìm file .pb nếu tên khác
            for f in os.listdir("."):
                if f.endswith(".pb") and "BT4-it332" in f:
                    pb_file = f
                    break

        run_cmd(f"python -m tf2onnx.convert --input {pb_file} --output model.onnx --inputs input:0 --outputs policy:0,value:0", "Chuyển đổi sang định dạng ONNX")
    
    model_path = os.path.abspath("model.onnx")

    # 4. Biên dịch nhân Engine
    os.chdir("src")
    run_cmd(f"make -j$(nproc) profile-build ARCH={ARCH} LIBS='-lonnxruntime'", "Biên dịch nhân Nextfish v2.0 (Hybrid)")
    
    engine_path = os.path.abspath("stockfish")

    # 5. Khởi động kiểm tra tích hợp
    if os.path.exists(engine_path):
        print("\n" + "="*60)
        print("🤖 KIỂM TRA TÍNH HỢP NHẤT NEXTFISH HYBRID")
        print("="*60)
        
        # Script UCI test
        test_cmds = [
            "uci",
            f"setoption name Lc0Policy_ModelPath value {model_path}",
            "setoption name Lc0Policy_Active value true",
            "isready",
            "position startpos",
            "go depth 22",
            "quit"
        ]
        
        process = subprocess.Popen(engine_path, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        for c in test_cmds:
            process.stdin.write(c + "\n")
            process.stdin.flush()
        
        while True:
            line = process.stdout.readline()
            if not line: break
            print(f"  > {line.strip()}")
            if "bestmove" in line: break
            
        print("\n" + "="*60)
        print(f"✨ HOÀN TẤT TRONG {round(time.time() - start_time, 2)} giây")
        print(f"📍 Engine path: {engine_path}")
        print(f"📍 Model path: {model_path}")
        print("="*60)
    else:
        print("[❌] Biên dịch thất bại. Không tìm thấy file engine.")

if __name__ == "__main__":
    main()
