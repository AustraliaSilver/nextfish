import sys
import time
from pathlib import Path
sys.path.append("Neuron Network")
from train_harenn import HARENNDataset

print("Testing HARENNDataset loading and binary caching on 'Neuron Network/data'...")
t0 = time.time()
ds = HARENNDataset("Neuron Network/data", use_cache=True, augment_flip=True)
print(f"Dataset successfully initialized in {time.time()-t0:.2f}s!")
print(f"Total base unique positions: {len(ds):,}")
print(f"With 4-way symmetry:         {len(ds) * 4:,} positions!")
