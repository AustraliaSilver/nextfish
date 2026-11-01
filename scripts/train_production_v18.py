import subprocess
import sys

cmd = [
    sys.executable, "-u", "Neuron Network/train_harenn.py",
    "--data", "Neuron Network/data",
    "--epochs", "6",
    "--finetune-eval-epochs", "4",
    "--batch-size", "8192",
    "--symmetry", "4",
    "--feature-dropout", "0.03",
    "--output", "Neuron Network/nextfish_v18.harenn",
    "--patience", "5"
]

print("Launching Production Training for HARENN v18 (Stockfish BCE WDL + Huber CP Loss)...")
print("Command:", " ".join(cmd))
sys.stdout.flush()

proc = subprocess.run(cmd)
print(f"Training finished with return code: {proc.returncode}")
