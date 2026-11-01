import os
import sys
import time
from pathlib import Path
from huggingface_hub import HfApi

bucket_id = "australia1/myrandombucket"
data_dir = "Neuron Network/data"
data_path = Path(data_dir)
data_path.mkdir(parents=True, exist_ok=True)

api = HfApi()
print(f"[HF-Sync] Listing bucket '{bucket_id}'...", flush=True)
files = list(api.list_bucket_tree(bucket_id))
jsonl_files = [f for f in files if getattr(f, "path", "").endswith(".jsonl")]
total_bytes = sum(getattr(f, "size", 0) or 0 for f in jsonl_files)
print(f"[HF-Sync] Found {len(jsonl_files)} JSONL files ({total_bytes / (1024*1024):.2f} MB total)", flush=True)

to_download = []
for f in jsonl_files:
    rel_path = getattr(f, "path", "")
    size = getattr(f, "size", 0) or 0
    local_file = data_path / Path(rel_path).name
    if local_file.exists() and local_file.stat().st_size == size:
        continue
    to_download.append((rel_path, str(local_file), size))

already_done = len(jsonl_files) - len(to_download)
print(f"[HF-Sync] Already present: {already_done}/{len(jsonl_files)} files. Need to download: {len(to_download)} files.", flush=True)

if not to_download:
    print("[HF-Sync] All files are already up-to-date!", flush=True)
    sys.exit(0)

t0 = time.time()
dl_pairs = [(r, l) for r, l, s in to_download]
# Download in batches of 10 to provide ongoing progress
batch_size = 10
for i in range(0, len(dl_pairs), batch_size):
    batch = dl_pairs[i:i+batch_size]
    b_start = time.time()
    api.download_bucket_files(bucket_id, batch)
    b_elapsed = time.time() - b_start
    print(f"[HF-Sync] Downloaded batch {i//batch_size + 1}/{(len(dl_pairs)+batch_size-1)//batch_size} ({len(batch)} files) in {b_elapsed:.1f}s", flush=True)

print(f"[HF-Sync] All files downloaded successfully in {time.time()-t0:.1f}s!", flush=True)
