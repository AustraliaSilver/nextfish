import os
import subprocess
import time

# --- CẤU HÌNH ---
REPO_URL = "https://github.com/AustraliaSilver/nextfish.git"
MODEL_URL = "https://storage.lczero.org/files/networks-contrib/BT4-1024x15x32h-swa-6147500-policytune-332.pb.gz"
ONNX_LIB_URL = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.1/onnxruntime-linux-x64-1.17.1.tgz"
ARCH = "x86-64-avx2"

def run_cmd(cmd, desc):
    print(f"\n[🚀] {desc}...")
    process = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in process.stdout:
        print(f"  {line.strip()}")
    process.wait()
    return process.returncode == 0

def main():
    start_time = time.time()
    working_dir = "/kaggle/working"
    os.chdir(working_dir)

    # 1. Chuẩn bị ONNX Runtime
    if not os.path.exists("onnxruntime-linux-x64-1.17.1"):
        run_cmd(f"wget {ONNX_LIB_URL} -O onnx.tgz && tar -zxvf onnx.tgz", "Tải ONNX Runtime")
    
    onnx_root = os.path.join(working_dir, "onnxruntime-linux-x64-1.17.1")
    onnx_inc, onnx_lib = os.path.join(onnx_root, "include"), os.path.join(onnx_root, "lib")

    # 2. Tải & Vá lỗi mã nguồn (Fix Makefile)
    repo_dir = os.path.join(working_dir, "nextfish")
    if os.path.exists(repo_dir): run_cmd(f"rm -rf {repo_dir}", "Dọn dẹp")
    run_cmd(f"git clone {REPO_URL} {repo_dir}", "Tải mã nguồn")
    
    src_dir = os.path.join(repo_dir, "src")
    os.chdir(src_dir)
    
    # Vá Makefile trực tiếp để link ONNX Runtime (Thêm CUDA support)
    print("[🛠️] Đang vá Makefile để hỗ trợ ONNX GPU...")
    patch_make = f"""
    sed -i 's|LDFLAGS = $(ENV_LDFLAGS) $(EXTRALDFLAGS)|LDFLAGS = $(ENV_LDFLAGS) $(EXTRALDFLAGS) -L{onnx_lib} -lonnxruntime -lpthread -ldl -lcudart -lcuda -Wl,-rpath,{onnx_lib}|' Makefile
    """
    run_cmd(patch_make, "Vá Makefile")

    # 3. Xử lý Model
    os.chdir(repo_dir)
    run_cmd(f"wget {MODEL_URL} -O model_raw.pb.gz && gunzip -f model_raw.pb.gz", "Chuẩn bị Model")
    pb_file = next((f for f in os.listdir(".") if f.endswith(".pb")), None)
    if pb_file:
        run_cmd("pip install tf2onnx onnxruntime-gpu", "Cài converter & GPU Runtime")
        # Chạy convert trên CPU (CUDA_VISIBLE_DEVICES="") để tránh lỗi bộ nhớ GPU
        # Sử dụng tên node chuẩn không có :0
        run_cmd(f"CUDA_VISIBLE_DEVICES='' python -m tf2onnx.convert --input {pb_file} --output model.onnx --inputs input:0 --outputs policy:0,value:0 --fold_const", "Convert Model")
    model_path = os.path.abspath("model.onnx")

    # 4. Biên dịch
    os.chdir(src_dir)
    # Bây giờ LDFLAGS đã được vá trong file, chỉ cần truyền CXXFLAGS
    make_flags = f"ARCH={ARCH} COMP=gcc CXXFLAGS='-I{onnx_inc}'"
    
    if run_cmd(f"make -j$(nproc) build {make_flags}", "Biên dịch Nextfish"):
        engine_path = os.path.abspath("stockfish")
        print("\n" + "="*60)
        print(f"✅ THÀNH CÔNG! Engine tại: {engine_path}")
        print(f"📍 Model tại: {model_path}")
        print("="*60)
    else:
        print("[❌] Biên dịch thất bại. Hãy kiểm tra lại log build.")

if __name__ == "__main__":
    main()