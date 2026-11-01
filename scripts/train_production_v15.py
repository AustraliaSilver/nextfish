import subprocess
import sys

cmd = [
    sys.executable, "-u", "Neuron Network/train_harenn.py",
    "--data", "Neuron Network/data",
    "--epochs", "8",
    "--finetune-eval-epochs", "4",
    "--batch-size", "8192",
    "--symmetry", "4",
    "--feature-dropout", "0.05",
    "--output", "Neuron Network/nextfish_v15.harenn",
    "--patience", "4"
]

print("Launching Production Training for HARENN v15...")
print("Command:", " ".join(cmd))
sys.stdout.flush()

proc = subprocess.run(cmd)
print(f"Training finished with return code: {proc.returncode}")
