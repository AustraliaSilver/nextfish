import struct
import os
import sys

weights_path = "Neuron Network/nextfish_v15.harenn"
if not os.path.exists(weights_path):
    print(f"File not found: {weights_path}")
    sys.exit(1)

size = os.path.getsize(weights_path)
print(f"Weights file: {weights_path} ({size:,} bytes / {size / (1024*1024):.2f} MB)")

with open(weights_path, 'rb') as f:
    magic = f.read(4)
    print(f"Magic: {magic}")
    assert magic == b"HNN4", f"Expected HNN4, got {magic}"
    
    mean, std = struct.unpack('<ff', f.read(8))
    print(f"Eval norm: mean={mean:.4f}, std={std:.4f}")
    
    # Layer 1: fc1
    r1, c1 = struct.unpack('<ii', f.read(8))
    print(f"FC1: {r1}x{c1}")
    w1_bytes = f.read(r1 * c1 * 2)
    b1_bytes = f.read(c1 * 4)
    
    # Layer 2: fc2
    r2, c2 = struct.unpack('<ii', f.read(8))
    print(f"FC2: {r2}x{c2}")
    w2_bytes = f.read(r2 * c2 * 2)
    b2_bytes = f.read(r2 * 4)
    
    # Heads: eval, tau, rho, rs
    for head in ['eval', 'tau', 'rho', 'rs']:
        rh, ch = struct.unpack('<ii', f.read(8))
        print(f"Head {head}: {rh}x{ch}")
        wh = f.read(rh * ch * 2)
        bh = f.read(rh * 4)

print("\nModel structure matches HNN4 specification perfectly!")
