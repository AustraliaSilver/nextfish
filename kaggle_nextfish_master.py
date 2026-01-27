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
    # Tự động dò tìm đường dẫn CUDA trên Kaggle
    cuda_path = "/usr/local/cuda/lib64"
    if not os.path.exists(cuda_path):
        cuda_path = "/usr/local/cuda/targets/x86_64-linux/lib"
    
    # Chèn thêm vào cuối Makefile để tránh bị ghi đè
    patch_cmd = f"""
    echo "LDFLAGS += -L{onnx_lib} -L{cuda_path} -L/usr/lib/x86_64-linux-gnu -lonnxruntime -lpthread -ldl -lcudart -lcuda" >> Makefile
    echo "LDFLAGS += -Wl,-rpath,{onnx_lib} -Wl,-rpath,{cuda_path} -Wl,-rpath,/usr/lib/x86_64-linux-gnu" >> Makefile
    """
    run_cmd(patch_cmd, "Vá Makefile (Append LDFLAGS)")

    # 3. Xử lý Model
    os.chdir(repo_dir)
    run_cmd(f"wget {MODEL_URL} -O model_raw.pb.gz && gunzip -f model_raw.pb.gz", "Chuẩn bị Model")
    pb_file = next((f for f in os.listdir(".") if f.endswith(".pb")), None)
    if pb_file:
        run_cmd("pip install tf2onnx onnxruntime-gpu", "Cài converter & GPU Runtime")
        
        # Script dò tìm node names
        inspect_script = f"""
import tensorflow as tf
def inspect_pb(pb_path):
    with tf.io.gfile.GFile(pb_path, 'rb') as f:
        graph_def = tf.compat.v1.GraphDef()
        graph_def.ParseFromString(f.read())
    nodes = [node.name for node in graph_def.node]
    inputs = [n for n in nodes if 'input' in n.lower()]
    policies = [n for n in nodes if 'policy' in n.lower() and 'softmax' in n.lower()]
    if not policies: policies = [n for n in nodes if 'policy' in n.lower()]
    values = [n for n in nodes if 'value' in n.lower() and ('tanh' in n.lower() or 'float' in n.lower())]
    if not values: values = [n for n in nodes if 'value' in n.lower()]
    print(f"DETECTED_INPUT: {{inputs[0] if inputs else 'input'}}:0")
    print(f"DETECTED_POLICY: {{policies[-1] if policies else 'policy'}}:0")
    print(f"DETECTED_VALUE: {{values[-1] if values else 'value'}}:0")

inspect_pb('{pb_file}')
"""
        with open("inspect_model.py", "w") as f: f.write(inspect_script)
        
        print("[🔍] Đang phân tích cấu trúc Model...")
        result = subprocess.check_output("python inspect_model.py", shell=True, text=True)
        print(result)
        
        inp = next(line.split(": ")[1] for line in result.split("\n") if "DETECTED_INPUT" in line)
        pol = next(line.split(": ")[1] for line in result.split("\n") if "DETECTED_POLICY" in line)
        val = next(line.split(": ")[1] for line in result.split("\n") if "DETECTED_VALUE" in line)

        run_cmd(f"CUDA_VISIBLE_DEVICES='' python -m tf2onnx.convert --input {pb_file} --output model.onnx --inputs {inp} --outputs {pol},{val} --fold_const", "Convert Model")
    model_path = os.path.abspath("model.onnx")

    # 4. Biên dịch
    os.chdir(src_dir)
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