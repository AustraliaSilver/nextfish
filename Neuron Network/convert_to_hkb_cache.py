import time
from pathlib import Path
import numpy as np

def main():
    cache_in = Path("Neuron Network/data/_halfka_sparse_cache_v1.npz")
    cache_out = Path("Neuron Network/data/_hkb_sparse_cache_v1.npz")

    print(f"[HKB Cache] Loading source HalfKA cache from {cache_in}...")
    t0 = time.time()
    data = np.load(cache_in, allow_pickle=False)
    us = data["us_indices"]
    them = data["them_indices"]
    eval_data = data["labels_eval"]
    tau_data = data["labels_tau"]
    rho_data = data["labels_rho"]
    rs_data = data["labels_rs"]

    print(f"[HKB Cache] Loaded {len(eval_data):,} positions in {time.time() - t0:.2f}s.")

    # Build 64-to-8 bucket LUT
    bucket_lut = np.zeros(65, dtype=np.uint16)
    for sq in range(64):
        r = sq // 8
        f = sq % 8
        if r == 0:
            b = 0 if f >= 5 else (1 if f <= 2 else 2)
        elif r == 1:
            b = 3 if f >= 5 else (4 if f <= 2 else 5)
        elif r in (2, 3):
            b = 6
        else:
            b = 7
        bucket_lut[sq] = b
    bucket_lut[64] = 8

    print("[HKB Cache] Transforming 45,056 HalfKA features into 5,632 King-Bucket features...")
    t1 = time.time()

    def transform(arr):
        is_pad = (arr == 45056)
        ksq = np.where(is_pad, 64, arr // 704)
        rem = np.where(is_pad, 0, arr % 704)
        b = bucket_lut[ksq]
        return np.where(is_pad, 5632, b * 704 + rem).astype(np.uint16)

    us_hkb = transform(us)
    them_hkb = transform(them)

    print(f"[HKB Cache] Transformation completed in {time.time() - t1:.2f}s. Saving to {cache_out}...")
    np.savez_compressed(
        cache_out,
        us_indices=us_hkb,
        them_indices=them_hkb,
        labels_eval=eval_data,
        labels_tau=tau_data,
        labels_rho=rho_data,
        labels_rs=rs_data
    )

    size_mb = cache_out.stat().st_size / (1024 * 1024)
    print(f"[HKB Cache] Done! Output cache size: {size_mb:.2f} MB. Total elapsed: {time.time() - t0:.1f}s.")

if __name__ == '__main__':
    main()
