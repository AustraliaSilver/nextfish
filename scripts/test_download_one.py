import time
import os
from huggingface_hub import HfApi

t0 = time.time()
api = HfApi()
os.makedirs('Neuron Network/data', exist_ok=True)
dest = 'Neuron Network/data/hnn_w1_40518781947_20261027_145836.jsonl'
api.download_bucket_files('australia1/myrandombucket', [('hnn_w1_40518781947_20261027_145836.jsonl', dest)])
print(f"Downloaded in {time.time()-t0:.2f}s, size: {os.path.getsize(dest):,} bytes")
