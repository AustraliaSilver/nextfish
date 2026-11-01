import subprocess
import sys
import os
import shutil
import time

print("=" * 65)
print("  HARENN PRODUCTION RETRAINING PIPELINE (v32)")
print("  Data Source: https://huggingface.co/buckets/australia1/myrandombucket")
print("  Total Data:  67 JSONL files | 3,077,540 Unique Positions | 12.31M Symmetries")
print("=" * 65)

cmd = [
    sys.executable, "-u", "Neuron Network/train_harenn.py",
    "--data", "Neuron Network/data",
    "--epochs", "6",
    "--finetune-eval-epochs", "4",
    "--batch-size", "8192",
    "--symmetry", "4",
    "--feature-dropout", "0.03",
    "--output", "Neuron Network/nextfish_v32.harenn",
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

out_file = "Neuron Network/nextfish_v32.harenn"
target_file = "Neuron Network/nextfish.harenn"

if os.path.exists(out_file):
    size_mb = os.path.getsize(out_file) / (1024 * 1024)
    print(f"Exported Model Size: {size_mb:.2f} MB")
    
    # Backup existing model
    backup_file = "Neuron Network/nextfish_backup.harenn"
    if os.path.exists(target_file):
        shutil.copyfile(target_file, backup_file)
        print(f"Backed up previous model to {backup_file}")
        
    # Install new model as production nextfish.harenn
    shutil.copyfile(out_file, target_file)
    print(f"Installed new model to {target_file}!")
else:
    print(f"[WARNING] Output file {out_file} not found!")
