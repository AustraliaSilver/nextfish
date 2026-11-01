import subprocess
import sys
import os
import shutil
import time

print("=" * 65)
print("  HARENN FAST-32 RETRAINING PIPELINE")
print("  Architecture: 768 -> 1024 -> 32 -> 4 heads (L1 Cache Optimized)")
print("  Data Source:  67 JSONL files | 3,077,540 Base Positions")
print("=" * 65)

cmd = [
    sys.executable, "-u", "Neuron Network/train_harenn.py",
    "--data", "Neuron Network/data",
    "--hidden1", "1024",
    "--hidden2", "32",
    "--epochs", "6",
    "--finetune-eval-epochs", "4",
    "--batch-size", "8192",
    "--symmetry", "4",
    "--feature-dropout", "0.03",
    "--output", "Neuron Network/nextfish_fast32.harenn",
    "--patience", "5"
]

print("\nStarting Training Command:")
print(" ".join(cmd))
sys.stdout.flush()

t0 = time.time()
proc = subprocess.run(cmd)
elapsed = time.time() - t0

if proc.returncode != 0:
    print(f"\n[ERROR] Training failed with exit code: {proc.returncode}")
    sys.exit(proc.returncode)

print(f"\n[SUCCESS] Training finished successfully in {elapsed/60:.2f} minutes!")

out_file = "Neuron Network/nextfish_fast32.harenn"
if os.path.exists(out_file):
    size_mb = os.path.getsize(out_file) / (1024 * 1024)
    print(f"Exported Model Size: {size_mb:.2f} MB")
else:
    print(f"[WARNING] Output file {out_file} not found!")
