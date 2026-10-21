import os
import subprocess
import threading
import time

# --- CẤU HÌNH ---
REPO_URL = "https://github.com/AustraliaSilver/nextfish.git"
MODEL_URL = "https://storage.lczero.org/files/networks-contrib/BT4-1024x15x32h-swa-6147500-policytune-332.pb.gz"
ONNX_LIB_URL = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.1/onnxruntime-linux-x64-gpu-1.17.1.tgz"
ARCH = "x86-64-avx2"

WORKING_DIR = "/kaggle/working"
MODEL_PATH = "/kaggle/working/model.onnx"
ONNX_DIR = os.path.join(WORKING_DIR, "onnxruntime-linux-x64-gpu-1.17.1")
ONNX_INC = os.path.join(ONNX_DIR, "include")
ONNX_LIB = os.path.join(ONNX_DIR, "lib")
CMD_TIMEOUT = 1800


def run_cmd(cmd, desc, cwd=None, timeout=CMD_TIMEOUT):
    print(f"\n[🚀] {desc}...")
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


def patch_makefile(src_dir):
    """Liên kết ONNX Runtime (GPU) vào Makefile, idempotent."""
    makefile = os.path.join(src_dir, "Makefile")
    with open(makefile, "r") as f:
        lines = f.readlines()
    lines = [l for l in lines if "onnxruntime" not in l]
    lines.append(f"LDFLAGS += -L{ONNX_LIB} -lonnxruntime -lpthread -ldl\n")
    lines.append(f"LDFLAGS += -Wl,-rpath,{ONNX_LIB}\n")
    with open(makefile, "w") as f:
        f.writelines(lines)
    print(f"[🛠️] Đã vá Makefile: -L{ONNX_LIB} -lonnxruntime")


def main():
    start_time = time.time()
    os.makedirs(WORKING_DIR, exist_ok=True)
    os.chdir(WORKING_DIR)

    # 1. Chuẩn bị ONNX Runtime (GPU)
    if not os.path.exists(ONNX_LIB):
        if not run_cmd(f"wget {ONNX_LIB_URL} -O onnx.tgz && tar -zxvf onnx.tgz", "Tải ONNX Runtime (GPU)"):
            print("[❌] Không tải được ONNX Runtime. Dừng.")
            return
    if not os.path.isdir(ONNX_LIB):
        print(f"[❌] Không tìm thấy thư viện ONNX Runtime tại {ONNX_LIB}. Dừng.")
        return

    # 2. Tải & Vá lỗi mã nguồn (Fix Makefile)
    repo_dir = os.path.join(WORKING_DIR, "nextfish")
    if os.path.exists(repo_dir):
        if not run_cmd(f"rm -rf {repo_dir}", "Dọn dẹp"):
            return
    if not run_cmd(f"git clone {REPO_URL} {repo_dir}", "Tải mã nguồn"):
        print("[❌] Không clone được mã nguồn. Dừng.")
        return

    src_dir = os.path.join(repo_dir, "src")
    if not os.path.isdir(src_dir):
        print(f"[❌] Không tìm thấy thư mục {src_dir}. Dừng.")
        return

    # Vá Makefile trực tiếp để link ONNX Runtime
    print("[🛠️] Đang vá Makefile...")
    patch_makefile(src_dir)

    # 3. Xử lý Model
    if not run_cmd(f"wget {MODEL_URL} -O model_raw.pb.gz && gunzip -f model_raw.pb.gz", "Chuẩn bị Model", cwd=WORKING_DIR):
        print("[❌] Không tải được model .pb. Dừng.")
        return
    pb_file = next((f for f in os.listdir(WORKING_DIR) if f.endswith(".pb")), None)
    if not pb_file:
        print("[❌] Không tìm thấy file .pb sau khi tải. Dừng.")
        return
    if not run_cmd("pip install tf2onnx", "Cài AI tools", cwd=WORKING_DIR):
        print("[❌] Không cài được tf2onnx. Dừng.")
        return
    # Chuyển đổi Model sang định dạng chuẩn 112 planes
    if not run_cmd(
        f"CUDA_VISIBLE_DEVICES='' python -m tf2onnx.convert --input {pb_file} --output {MODEL_PATH} "
        "--inputs input:0 --outputs policy_output/Softmax:0,value_output/Tanh:0 --fold_const",
        "Convert Model",
        cwd=WORKING_DIR,
    ):
        print("[❌] Chuyển đổi model sang ONNX thất bại. Dừng.")
        return
    if not os.path.exists(MODEL_PATH):
        print(f"[❌] Không tìm thấy {MODEL_PATH} sau khi convert. Dừng.")
        return

    # 4. Biên dịch
    os.chdir(src_dir)
    make_flags = f"ARCH={ARCH} COMP=gcc CXXFLAGS='-I{ONNX_INC}'"

    if run_cmd(f"make -j$(nproc) build {make_flags}", "Biên dịch Nextfish"):
        engine_path = os.path.join(src_dir, "stockfish")
        print("\n" + "="*60)
        print(f"✅ THÀNH CÔNG! Engine tại: {engine_path}")
        print(f"📍 Model tại: {MODEL_PATH}")
        print(f"⏱️ Thời gian: {time.time() - start_time:.0f}s")
        print("="*60)
    else:
        print("[❌] Biên dịch thất bại. Hãy kiểm tra lại log build.")


if __name__ == "__main__":
    main()
