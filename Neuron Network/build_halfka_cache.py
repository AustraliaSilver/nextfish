"""
Build HalfKA sparse cache from all JSONL datasets in Neuron Network/data.
Features: 45,056 dimensions per perspective.
Padding index: 45,056.
Storage: uint16 arrays for us_indices and them_indices.
"""

import os
import glob
import json
import time
from pathlib import Path
from multiprocessing import Pool, cpu_count
import numpy as np

PADDING_FEATURE_IDX = 45056
MAX_ACTIVE_FEATURES = 32

P2PT_W = {'P': 0, 'N': 1, 'B': 2, 'R': 3, 'Q': 4, 'p': 5, 'n': 6, 'b': 7, 'r': 8, 'q': 9, 'k': 10}
P2PT_B = {'p': 0, 'n': 1, 'b': 2, 'r': 3, 'q': 4, 'P': 5, 'N': 6, 'B': 7, 'R': 8, 'Q': 9, 'K': 10}


def parse_file(file_path_str):
    records = []
    seen = set()
    with open(file_path_str, 'r', encoding='utf-8') as f:
        for line in f:
            if not line.strip():
                continue
            try:
                pos = json.loads(line)
            except Exception:
                continue

            fen = pos.get('fen', '')
            if not fen:
                continue

            # Deduplicate by FEN within the file
            if fen in seen:
                continue
            seen.add(fen)

            stm = pos.get('stm')
            if stm is None:
                stm = 1 if ' b ' in fen else 0
            else:
                stm = int(stm)

            rows = fen.split()[0].split('/')
            if len(rows) != 8:
                continue

            w_ksq = None
            b_ksq = None
            pieces = []  # (sq, ch)

            for r_idx, row in enumerate(rows):
                rank = 7 - r_idx
                file = 0
                for ch in row:
                    if ch.isdigit():
                        file += int(ch)
                    else:
                        sq = rank * 8 + file
                        if ch == 'K':
                            w_ksq = sq
                        elif ch == 'k':
                            b_ksq = sq
                        pieces.append((sq, ch))
                        file += 1

            if w_ksq is None or b_ksq is None:
                continue

            # White perspective features
            w_feats = []
            for sq, ch in pieces:
                if ch == 'K':
                    continue
                pt = P2PT_W.get(ch)
                if pt is not None:
                    w_feats.append(w_ksq * 704 + pt * 64 + sq)

            # Black perspective features
            b_feats = []
            b_ksq_flip = b_ksq ^ 56
            for sq, ch in pieces:
                if ch == 'k':
                    continue
                pt = P2PT_B.get(ch)
                if pt is not None:
                    b_feats.append(b_ksq_flip * 704 + pt * 64 + (sq ^ 56))

            w_arr = np.full(MAX_ACTIVE_FEATURES, PADDING_FEATURE_IDX, dtype=np.uint16)
            b_arr = np.full(MAX_ACTIVE_FEATURES, PADDING_FEATURE_IDX, dtype=np.uint16)
            w_len = min(len(w_feats), MAX_ACTIVE_FEATURES)
            b_len = min(len(b_feats), MAX_ACTIVE_FEATURES)
            w_arr[:w_len] = w_feats[:w_len]
            b_arr[:b_len] = b_feats[:b_len]

            us = w_arr if stm == 0 else b_arr
            them = b_arr if stm == 0 else w_arr

            eval_score = float(pos.get('eval_score', 0.0))
            eval_score = max(-1000.0, min(1000.0, eval_score))
            # In JSONL data, eval_score is ALREADY relative to side to move!
            eval_pawns = eval_score / 100.0

            tau = float(pos.get('tau', 0.5))
            rho = float(pos.get('rho', 0.3))
            rs = float(pos.get('rs', 0.5))

            records.append((fen, us, them, eval_pawns, tau, rho, rs))

    return records


def main():
    data_dir = Path("Neuron Network/data")
    cache_path = data_dir / "_halfka_sparse_cache_v1.npz"

    files = sorted(data_dir.glob("*.jsonl"))
    print(f"[HalfKA Cache] Found {len(files)} JSONL files to process...")

    workers = max(1, min(8, cpu_count()))
    print(f"[HalfKA Cache] Processing with {workers} worker processes...")
    t0 = time.time()

    file_strs = [str(f) for f in files]
    with Pool(workers) as pool:
        all_results = pool.map(parse_file, file_strs)

    t_parse = time.time() - t0
    total_parsed = sum(len(r) for r in all_results)
    print(f"[HalfKA Cache] Parsed {total_parsed:,} records in {t_parse:.1f}s ({(total_parsed/t_parse):,.0f} pos/sec)")

    # Global deduplication across all files
    print("[HalfKA Cache] Global deduplicating by FEN...")
    seen_fens = set()
    us_list = []
    them_list = []
    eval_list = []
    tau_list = []
    rho_list = []
    rs_list = []

    for file_records in all_results:
        for fen, us, them, ev, tau, rho, rs in file_records:
            if fen not in seen_fens:
                seen_fens.add(fen)
                us_list.append(us)
                them_list.append(them)
                eval_list.append(ev)
                tau_list.append(tau)
                rho_list.append(rho)
                rs_list.append(rs)

    n_unique = len(us_list)
    print(f"[HalfKA Cache] {n_unique:,} unique positions retained.")

    print("[HalfKA Cache] Converting to compact NumPy arrays...")
    us_indices = np.array(us_list, dtype=np.uint16)
    them_indices = np.array(them_list, dtype=np.uint16)
    labels_eval = np.array(eval_list, dtype=np.float32)
    labels_tau = np.array(tau_list, dtype=np.float32)
    labels_rho = np.array(rho_list, dtype=np.float32)
    labels_rs = np.array(rs_list, dtype=np.float32)

    print(f"[HalfKA Cache] Saving to {cache_path}...")
    np.savez_compressed(
        cache_path,
        us_indices=us_indices,
        them_indices=them_indices,
        labels_eval=labels_eval,
        labels_tau=labels_tau,
        labels_rho=labels_rho,
        labels_rs=labels_rs
    )

    t_total = time.time() - t0
    file_mb = cache_path.stat().st_size / (1024 * 1024)
    print(f"[HalfKA Cache] Completed in {t_total:.1f}s. Cache size: {file_mb:.1f} MB.")


if __name__ == '__main__':
    main()
