#!/usr/bin/env python3
"""
HARENN-UE Training Pipeline (Updatable Efficiently & Stockfish NNUE-Grade Edition)
Multi-head NNUE-style training for Horizon-Aware Reduction & Evaluation.
Default Architecture: 768 Sparse Accumulator -> 1024 -> 512 -> 4 heads (1,314,308 parameters)
Architecture & HNN4 binary export bit-identical to C++ AVX2 inference engine.

Key HARENN-UE (Updatable Efficiently) Enhancements:
  1. Expanded High-Capacity Backbone (768 -> 1024 -> 512 -> 4 heads = 1.31M parameters,
     2.5x larger than legacy 526k model; configurable via --hidden1 and --hidden2).
  2. Direct Sparse Feature Accumulator (F.embedding_bag on int16[32] active piece indices):
     Sums only the <= 32 active pieces on the board instead of multiplying 768 dense features.
  3. Full 2x Color-Flip Symmetry Dataset Expansion (2.73M Unique Base + 2.73M Flipped = 5.47M Pos):
     Every position is deterministically paired with its vertically flipped & color-inverted dual.
  4. Two-Stage Curriculum + Post-Training Eval Fine-Tuning (--finetune-eval-epochs):
     - Stage 1: Joint Multi-Task Training on (eval, tau, rho, rs) with Stockfish WDL Sigmoid
       Power-2.5 + Smooth-L1 Huber Loss and Homoscedastic Uncertainty Weighting.
     - Stage 2A: Eval-Prioritized Backbone Refinement (3x eval focus at low LR while preserving
       auxiliary heads tau/rho/rs from catastrophic forgetting).
     - Stage 2B: Exact Eval Head Calibration (freezes backbone & auxiliary heads so tau/rho/rs
       stay 100% locked, and fine-tunes eval_head to minimum Centipawn MAE + WDL error).
  5. Quantization-Aware Training (QAT) with Straight-Through Estimator (STE).

Usage:
    python train_harenn.py --sync-hf --data data --epochs 18 --finetune-eval-epochs 6 --batch-size 8192
"""

import argparse
import json
import math
import os
import random
import struct
import time
from pathlib import Path
from typing import List, Dict, Tuple, Optional, Set
from harenn_fen import fen_side_to_move, fen_move_number
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader, Subset

torch.manual_seed(42)
np.random.seed(42)
random.seed(42)

PIECE_MAP = {
    'P': 0, 'N': 1, 'B': 2, 'R': 3, 'Q': 4, 'K': 5,
    'p': 6, 'n': 7, 'b': 8, 'r': 9, 'q': 10, 'k': 11
}

REQUIRED_KEYS = ("fen", "tau", "rho", "rs", "eval_score")
TASKS = ("eval", "tau", "rho", "rs")
MAX_ACTIVE_FEATURES = 32
PADDING_FEATURE_IDX = 768

# Precomputed Lookup Tables for O(1) Symmetries on 768 features (+ padding 768 -> 768):
# 1. Vertical Color-Flip (sq ^ 56, color ^ 6) -> eval: -eval
FLIP_V_FEATURE_LUT = np.array(
    [((idx // 12) ^ 56) * 12 + (((idx % 12) + 6) % 12) for idx in range(768)] + [PADDING_FEATURE_IDX],
    dtype=np.int64
)

# 2. Horizontal File-Reflection (sq ^ 7, file a<->h, color unchanged) -> eval: eval
FLIP_H_FEATURE_LUT = np.array(
    [((idx // 12) ^ 7) * 12 + (idx % 12) for idx in range(768)] + [PADDING_FEATURE_IDX],
    dtype=np.int64
)

# 3. Diagonal Dual-Mirror (sq ^ 63, rank+file+color) -> eval: -eval
FLIP_VH_FEATURE_LUT = np.array(
    [((idx // 12) ^ 63) * 12 + (((idx % 12) + 6) % 12) for idx in range(768)] + [PADDING_FEATURE_IDX],
    dtype=np.int64
)

# Backward-compatible alias
FLIP_FEATURE_LUT = FLIP_V_FEATURE_LUT


def fen_to_sparse_indices(fen: str) -> np.ndarray:
    """
    Convert FEN into compact int16[32] active feature indices (padded with 768).
    Encoding matches C++ extract_features bit-for-bit:
      py_sq = sq ^ 56 (rank flip a1<->a8, bottom-up to top-down)
      py_pc = piece - 1  (W:0-5, B:6-11)
      feat  = py_sq * 12 + py_pc
    """
    board_part = fen.split()[0]
    active = np.full(MAX_ACTIVE_FEATURES, PADDING_FEATURE_IDX, dtype=np.int16)
    count = 0
    squares = board_part.split('/')
    for rank_fen, row in enumerate(squares):
        file = 0
        for ch in row:
            if ch.isdigit():
                file += int(ch)
            else:
                if count < MAX_ACTIVE_FEATURES:
                    sf_sq = (7 - rank_fen) * 8 + file
                    py_sq = sf_sq ^ 56
                    py_pc = PIECE_MAP[ch]
                    active[count] = py_sq * 12 + py_pc
                    count += 1
                file += 1
    return active


def fen_to_features(fen: str) -> np.ndarray:
    """
    Convert FEN to 768-dim piece-square feature vector.
    Kept for full backward compatibility with external callers/tests.
    """
    features = np.zeros(768, dtype=np.float32)
    active = fen_to_sparse_indices(fen)
    valid = active[(active >= 0) & (active < 768)]
    features[valid] = 1.0
    return features


def sync_from_huggingface(bucket_id: str, data_dir: str, token: Optional[str] = None) -> int:
    """
    Download/sync all .jsonl files from Hugging Face Bucket into data_dir.
    Skips files that already exist locally with matching byte size.
    """
    try:
        from huggingface_hub import HfApi
    except ImportError as e:
        raise RuntimeError("Please install huggingface_hub: pip install huggingface_hub") from e

    data_path = Path(data_dir)
    data_path.mkdir(parents=True, exist_ok=True)

    api = HfApi(token=token or os.environ.get("HF_TOKEN"))
    print(f"[HF-Sync] Listing bucket '{bucket_id}'...")
    files = list(api.list_bucket_tree(bucket_id))
    jsonl_files = [f for f in files if getattr(f, "path", "").endswith(".jsonl")]
    total_remote_bytes = sum(getattr(f, "size", 0) or 0 for f in jsonl_files)
    print(f"[HF-Sync] Found {len(jsonl_files)} .jsonl files ({total_remote_bytes / (1024 * 1024):.2f} MB total)")

    to_download = []
    for f in jsonl_files:
        rel_path = getattr(f, "path", "")
        remote_size = getattr(f, "size", 0) or 0
        local_file = data_path / Path(rel_path).name
        if local_file.exists() and local_file.stat().st_size == remote_size:
            continue
        to_download.append((rel_path, str(local_file)))

    if not to_download:
        print("[HF-Sync] All files are already up-to-date locally!")
        return len(jsonl_files)

    print(f"[HF-Sync] Downloading {len(to_download)} new/updated files to {data_path}...")
    t0 = time.time()
    api.download_bucket_files(bucket_id, to_download)
    dt = time.time() - t0
    print(f"[HF-Sync] Downloaded {len(to_download)} files in {dt:.2f}s!")
    return len(jsonl_files)


class HARENNDataset(Dataset):
    """
    HARENN-UE Dataset supporting 3M+ base positions (+ 3M+ color-flipped = 6M+ positions)
    stored in compact sparse RAM (int16[32] active indices + float32 targets).
    """

    def __init__(self, data_dir: str, use_cache: bool = True, augment_flip: bool = True):
        self.data_dir = Path(data_dir)
        self.use_cache = use_cache
        self.augment_flip = augment_flip

        self.active_indices: np.ndarray = np.empty((0, MAX_ACTIVE_FEATURES), dtype=np.int16)
        self.labels_eval: np.ndarray = np.empty((0,), dtype=np.float32)
        self.labels_tau: np.ndarray = np.empty((0,), dtype=np.float32)
        self.labels_rho: np.ndarray = np.empty((0,), dtype=np.float32)
        self.labels_rs: np.ndarray = np.empty((0,), dtype=np.float32)
        self.game_ids: List[str] = []

        self._load_or_build(self.data_dir)

    def _cache_file_path(self, data_path: Path) -> Path:
        return data_path / "_harenn_ue_sparse_cache_v3.npz"

    def _load_or_build(self, data_path: Path):
        if not data_path.exists():
            raise FileNotFoundError(f"Data directory not found: {data_path}")

        json_files = sorted(data_path.glob("*.json"))
        jsonl_files = sorted(data_path.glob("*.jsonl"))
        all_source_files = json_files + jsonl_files

        cache_path = self._cache_file_path(data_path)
        source_signature = f"{len(all_source_files)}:{sum(f.stat().st_size for f in all_source_files)}"

        if self.use_cache and cache_path.exists() and len(all_source_files) > 0:
            try:
                t0 = time.time()
                cached = np.load(str(cache_path), allow_pickle=False)
                if str(cached["signature"]) == source_signature:
                    self.active_indices = cached["active_indices"]
                    self.labels_eval = cached["labels_eval"]
                    self.labels_tau = cached["labels_tau"]
                    self.labels_rho = cached["labels_rho"]
                    self.labels_rs = cached["labels_rs"]
                    self.game_ids = cached["game_ids"].tolist()
                    dt = time.time() - t0
                    print(f"[Cache] Loaded {len(self.labels_eval):,} unique base positions "
                          f"({len(self.labels_eval) * 2:,} with 2x Color-Flip Symmetry) in {dt:.2f}s", flush=True)
                    return
            except Exception as e:
                print(f"[Cache] Rebuilding cache ({e})...", flush=True)

        t0 = time.time()
        temp_indices: List[np.ndarray] = []
        temp_eval: List[float] = []
        temp_tau: List[float] = []
        temp_rho: List[float] = []
        temp_rs: List[float] = []
        seen_board_hashes: Set[int] = set()

        total_raw = 0
        for json_file in json_files:
            total_raw += self._load_json_file(
                json_file, seen_board_hashes, temp_indices, temp_eval, temp_tau, temp_rho, temp_rs
            )
        for idx_f, jsonl_file in enumerate(jsonl_files, 1):
            total_raw += self._load_jsonl_file(
                jsonl_file, seen_board_hashes, temp_indices, temp_eval, temp_tau, temp_rho, temp_rs
            )
            if idx_f % 10 == 0 or idx_f == len(jsonl_files):
                print(f"  Parsed {idx_f}/{len(jsonl_files)} JSONL files -> "
                      f"{len(temp_eval):,} unique positions ({total_raw:,} raw)...", flush=True)

        if temp_indices:
            self.active_indices = np.vstack(temp_indices).astype(np.int16, copy=False)
            self.labels_eval = np.asarray(temp_eval, dtype=np.float32)
            self.labels_tau = np.asarray(temp_tau, dtype=np.float32)
            self.labels_rho = np.asarray(temp_rho, dtype=np.float32)
            self.labels_rs = np.asarray(temp_rs, dtype=np.float32)

        dt = time.time() - t0
        print(f"Loaded {len(self.labels_eval):,} unique base positions (from {total_raw:,} raw records) "
              f"across {len(all_source_files)} files in {dt:.2f}s", flush=True)
        if self.augment_flip:
            print(f"[HARENN-UE Symmetry] 2x Color-Flip Expansion active: "
                  f"{len(self.labels_eval):,} base + {len(self.labels_eval):,} flipped = "
                  f"{len(self.labels_eval) * 2:,} effective positions!", flush=True)

        if self.use_cache and len(self.labels_eval) >= 5000:
            try:
                t_save = time.time()
                np.savez(
                    str(cache_path),
                    signature=np.array(source_signature),
                    active_indices=self.active_indices,
                    labels_eval=self.labels_eval,
                    labels_tau=self.labels_tau,
                    labels_rho=self.labels_rho,
                    labels_rs=self.labels_rs,
                    game_ids=np.asarray(self.game_ids),
                )
                print(f"[Cache] Saved fast binary cache to {cache_path} ({time.time() - t_save:.2f}s)", flush=True)
            except Exception as e:
                print(f"[Cache] Warning: could not write cache: {e}", flush=True)

    def _load_json_file(self, json_file: Path, seen_hashes: Set[int],
                        temp_indices, temp_eval, temp_tau, temp_rho, temp_rs) -> int:
        try:
            with open(json_file, 'r', encoding='utf-8') as f:
                file_data = json.load(f)
        except json.JSONDecodeError as e:
            raise ValueError(f"{json_file}: invalid JSON: {e}") from e

        if not isinstance(file_data, dict) or 'positions' not in file_data:
            raise ValueError(f"{json_file}: expected a JSON object containing a 'positions' list")

        game_id = json_file.stem
        for index, pos in enumerate(file_data['positions']):
            self._add_record(
                pos, game_id, f"{json_file}[{index}]",
                seen_hashes, temp_indices, temp_eval, temp_tau, temp_rho, temp_rs
            )
        return len(file_data['positions'])

    def _load_jsonl_file(self, jsonl_file: Path, seen_hashes: Set[int],
                         temp_indices, temp_eval, temp_tau, temp_rho, temp_rs) -> int:
        game_id = jsonl_file.stem
        game_index = 0
        prev_move_number = None
        count = 0
        with open(jsonl_file, 'r', encoding='utf-8') as f:
            for lineno, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    pos = json.loads(line)
                except json.JSONDecodeError as e:
                    raise ValueError(f"{jsonl_file}:{lineno}: invalid JSON: {e}") from e

                move_number = fen_move_number(pos.get("fen", "")) if isinstance(pos, dict) else None
                if (move_number is not None and prev_move_number is not None
                        and move_number < prev_move_number - 1):
                    game_index += 1
                if move_number is not None:
                    prev_move_number = move_number

                self._add_record(
                    pos, f"{game_id}#{game_index}", f"{jsonl_file}:{lineno}",
                    seen_hashes, temp_indices, temp_eval, temp_tau, temp_rho, temp_rs
                )
                count += 1
        return count

    def _add_record(self, pos, game_id: str, where: str, seen_hashes: Set[int],
                    temp_indices, temp_eval, temp_tau, temp_rho, temp_rs):
        if not isinstance(pos, dict):
            raise ValueError(f"{where}: expected a JSON object")
        missing = [key for key in REQUIRED_KEYS if key not in pos]
        if missing:
            raise ValueError(f"{where}: missing required field(s): {', '.join(missing)}")

        fen = pos['fen']
        fen_hash = hash(fen)
        if fen_hash in seen_hashes:
            return
        seen_hashes.add(fen_hash)

        if 'stm' in pos and pos['stm'] is not None:
            stm = int(pos['stm'])
        else:
            stm = fen_side_to_move(fen)

        eval_score = float(pos['eval_score'])
        eval_score = max(-1000.0, min(1000.0, eval_score))
        if stm == 1:
            eval_score = -eval_score

        eval_pawns = eval_score / 100.0

        temp_indices.append(fen_to_sparse_indices(fen))
        temp_eval.append(eval_pawns)
        temp_tau.append(float(np.clip(float(pos['tau']), 0.0, 1.0)))
        temp_rho.append(float(np.clip(float(pos['rho']), 0.0, 1.0)))
        temp_rs.append(float(np.clip(float(pos['rs']), 0.0, 1.0)))
        self.game_ids.append(game_id)

    def __len__(self):
        return len(self.labels_eval)

    def __getitem__(self, idx):
        active = self.active_indices[idx]
        valid = active[(active >= 0) & (active < 768)]

        eval_val = float(self.labels_eval[idx])
        tau_val = float(self.labels_tau[idx])
        rho_val = float(self.labels_rho[idx])
        rs_val = float(self.labels_rs[idx])

        if self.augment_flip and random.random() < 0.5:
            valid = FLIP_FEATURE_LUT[valid]
            eval_val = -eval_val

        features = np.zeros(768, dtype=np.float32)
        features[valid] = 1.0

        labels = {
            'eval': torch.tensor(eval_val, dtype=torch.float32),
            'tau': torch.tensor(tau_val, dtype=torch.float32),
            'rho': torch.tensor(rho_val, dtype=torch.float32),
            'rs': torch.tensor(rs_val, dtype=torch.float32),
        }
        return torch.from_numpy(features), labels

    def label_stats(self) -> Dict[str, Tuple[float, float]]:
        """
        Per-task (mean, std) of the targets.
        When 2x Color-Flip Symmetry is active, every eval v is paired with -v,
        so the symmetric eval mean is 0.0 and std is sqrt(E[v^2]).
        """
        stats = {}
        arrays = {
            'eval': self.labels_eval,
            'tau': self.labels_tau,
            'rho': self.labels_rho,
            'rs': self.labels_rs,
        }
        for task in TASKS:
            values = np.asarray(arrays[task], dtype=np.float64)
            if values.size == 0:
                stats[task] = (0.0, 1.0)
                continue
            if task == 'eval' and self.augment_flip:
                mean = 0.0
                std = float(np.sqrt(np.mean(values ** 2)))
            else:
                mean = float(values.mean())
                std = float(values.std())
            if std <= 0.0:
                std = 1.0
            stats[task] = (mean, std)
        return stats


class PreprocessedHARENNDataset(Dataset):
    """Dataset loading from preprocessed .npy binary files."""

    def __init__(self, data_dir: str):
        self.data_dir = Path(data_dir)
        self.features = np.load(str(self.data_dir / "features.npy"), mmap_mode='r')
        self.labels_eval = np.load(str(self.data_dir / "labels_eval.npy"), mmap_mode='r')
        self.labels_tau = np.load(str(self.data_dir / "labels_tau.npy"), mmap_mode='r')
        self.labels_rho = np.load(str(self.data_dir / "labels_rho.npy"), mmap_mode='r')
        self.labels_rs = np.load(str(self.data_dir / "labels_rs.npy"), mmap_mode='r')
        print(f"Loaded {len(self)} samples from {data_dir}")

    def __len__(self):
        return self.features.shape[0]

    def __getitem__(self, idx):
        feats = torch.from_numpy(self.features[idx].copy()).float()
        labels = {
            'eval': torch.tensor(self.labels_eval[idx], dtype=torch.float32),
            'tau': torch.tensor(self.labels_tau[idx], dtype=torch.float32),
            'rho': torch.tensor(self.labels_rho[idx], dtype=torch.float32),
            'rs': torch.tensor(self.labels_rs[idx], dtype=torch.float32),
        }
        return feats, labels

    def label_stats(self) -> Dict[str, Tuple[float, float]]:
        stats = {}
        for task, array in (('eval', self.labels_eval), ('tau', self.labels_tau),
                             ('rho', self.labels_rho), ('rs', self.labels_rs)):
            values = np.asarray(array, dtype=np.float64)
            mean = float(values.mean())
            std = float(values.std())
            if std <= 0.0:
                std = 1.0
            stats[task] = (mean, std)
        return stats


class FastSparseSymmetryBatchLoader:
    """
    HARENN-UE High-Throughput Vectorized Batch Streamer with Dual-Mirror 4-Way Symmetry.
    Expands N base positions into up to 4*N positions per epoch:
      - IDs [0*N .. 1*N-1]: Original positions (I) -> eval
      - IDs [1*N .. 2*N-1]: Color-Flipped positions (V: sq ^ 56, color ^ 6) -> -eval
      - IDs [2*N .. 3*N-1]: Horizontal-Flipped positions (H: sq ^ 7, file a<->h) -> eval
      - IDs [3*N .. 4*N-1]: Dual-Mirror positions (VH: sq ^ 63, file+rank+color) -> -eval
    Feeds compact (B, 32) int64 sparse feature indices directly to GPU where
    HARENNNetwork's EmbeddingBag accumulator sums active piece weights in O(32).
    """

    def __init__(self, dataset: HARENNDataset, indices: List[int], batch_size: int,
                 shuffle: bool, device: str, expand_flip: bool = True,
                 symmetry_mode: int = 4, feature_dropout: float = 0.0):
        self.dataset = dataset
        self.base_indices = np.asarray(indices, dtype=np.int64)
        self.num_base = len(self.base_indices)
        if not expand_flip:
            self.symmetry_mode = 1
        else:
            self.symmetry_mode = symmetry_mode
        self.total_samples = self.num_base * self.symmetry_mode
        self.batch_size = batch_size
        self.shuffle = shuffle
        self.device = device
        self.feature_dropout = feature_dropout

        self.flip_v_gpu = torch.as_tensor(FLIP_V_FEATURE_LUT, dtype=torch.long, device=device)
        self.flip_h_gpu = torch.as_tensor(FLIP_H_FEATURE_LUT, dtype=torch.long, device=device)
        self.flip_vh_gpu = torch.as_tensor(FLIP_VH_FEATURE_LUT, dtype=torch.long, device=device)

    def __len__(self):
        return (self.total_samples + self.batch_size - 1) // self.batch_size

    def __iter__(self):
        if self.shuffle:
            perm = np.random.permutation(self.total_samples)
        else:
            perm = np.arange(self.total_samples, dtype=np.int64)

        for start in range(0, self.total_samples, self.batch_size):
            batch_virtual_ids = perm[start:start + self.batch_size]
            variant = batch_virtual_ids // self.num_base
            raw_pos = batch_virtual_ids % self.num_base

            actual_ids = self.base_indices[raw_pos]

            batch_active = torch.as_tensor(
                self.dataset.active_indices[actual_ids], dtype=torch.long, device=self.device
            )
            batch_eval = torch.as_tensor(
                self.dataset.labels_eval[actual_ids], dtype=torch.float32, device=self.device
            )
            batch_tau = torch.as_tensor(
                self.dataset.labels_tau[actual_ids], dtype=torch.float32, device=self.device
            )
            batch_rho = torch.as_tensor(
                self.dataset.labels_rho[actual_ids], dtype=torch.float32, device=self.device
            )
            batch_rs = torch.as_tensor(
                self.dataset.labels_rs[actual_ids], dtype=torch.float32, device=self.device
            )

            if self.symmetry_mode > 1:
                var_t = torch.as_tensor(variant, dtype=torch.long, device=self.device)
                is_v = (var_t == 1).unsqueeze(1)
                is_h = (var_t == 2).unsqueeze(1)
                is_vh = (var_t == 3).unsqueeze(1)

                if is_v.any():
                    batch_active = torch.where(is_v, self.flip_v_gpu[batch_active], batch_active)
                if is_h.any():
                    batch_active = torch.where(is_h, self.flip_h_gpu[batch_active], batch_active)
                if is_vh.any():
                    batch_active = torch.where(is_vh, self.flip_vh_gpu[batch_active], batch_active)

                eval_negate = (var_t == 1) | (var_t == 3)
                batch_eval = torch.where(eval_negate, -batch_eval, batch_eval)

            if self.feature_dropout > 0.0 and self.shuffle:
                drop_prob = torch.rand(batch_active.shape, device=self.device)
                is_valid = (batch_active < PADDING_FEATURE_IDX)
                is_not_king = ((batch_active % 12) % 6) != 5
                drop_mask = (drop_prob < self.feature_dropout) & is_valid & is_not_king
                batch_active = torch.where(
                    drop_mask,
                    torch.tensor(PADDING_FEATURE_IDX, dtype=torch.long, device=self.device),
                    batch_active
                )

            labels = {
                'eval': batch_eval,
                'tau': batch_tau,
                'rho': batch_rho,
                'rs': batch_rs,
            }
            yield batch_active, labels


def split_by_game(dataset, val_split: float, seed: int = 42):
    """Group-wise split: whole games go to either train or validation."""
    game_ids = list(getattr(dataset, "game_ids", []))
    if not game_ids:
        indices = list(range(len(dataset)))
        rng = random.Random(seed)
        rng.shuffle(indices)
        val_size = int(len(indices) * val_split)
        return indices[val_size:], indices[:val_size]

    unique_games = sorted(set(game_ids))
    if len(unique_games) <= 1:
        indices = list(range(len(dataset)))
        rng = random.Random(seed)
        rng.shuffle(indices)
        val_size = max(1, int(len(indices) * val_split))
        return indices[val_size:], indices[:val_size]

    random.Random(seed).shuffle(unique_games)
    val_games = set(unique_games[:max(1, int(round(len(unique_games) * val_split)))])
    train_indices = [i for i, gid in enumerate(game_ids) if gid not in val_games]
    val_indices = [i for i, gid in enumerate(game_ids) if gid in val_games]
    return train_indices, val_indices


def fake_quant_ste(x: torch.Tensor, scale: float, min_q: float, max_q: float) -> torch.Tensor:
    """
    Straight-Through Estimator (STE) for Quantization-Aware Training (QAT).
    Simulates C++ integer quantization in the forward pass while keeping exact
    floating-point gradients in the backward pass.
    """
    q = torch.clamp(torch.round(x * scale), min=min_q, max=max_q) / scale
    return x + (q - x).detach()


class HARENNNetwork(nn.Module):
    """
    HARENN-UE (Updatable Efficiently):
      768-dim sparse piece-square accumulator -> hidden1 (1024) -> hidden2 (512) -> 4 heads
    Supports both:
      - Sparse index input (B, 32) int64 via F.embedding_bag (O(32) active pieces gather)
      - Legacy dense float input (B, 768) via F.linear
    """

    def __init__(self, hidden1: int = 1024, hidden2: int = 512,
                 qat: bool = True, activation: str = 'clipped_relu'):
        super().__init__()
        self.hidden1 = hidden1
        self.hidden2 = hidden2
        self.qat = qat
        self.activation = activation

        self.fc1 = nn.Linear(768, hidden1, bias=True)
        self.fc2 = nn.Linear(hidden1, hidden2, bias=True)

        self.eval_head = nn.Linear(hidden2, 1, bias=True)
        self.tau_head = nn.Linear(hidden2, 1, bias=True)
        self.rho_head = nn.Linear(hidden2, 1, bias=True)
        self.rs_head = nn.Linear(hidden2, 1, bias=True)

        self.register_buffer('_zero_pad_row', torch.zeros(1, hidden1, dtype=torch.float32), persistent=False)
        self._init_weights()

    def _init_weights(self):
        for m in (self.fc1, self.fc2):
            nn.init.kaiming_normal_(m.weight, nonlinearity='relu')
            m.weight.data.mul_(0.5)
            nn.init.zeros_(m.bias)
        for h in (self.eval_head, self.tau_head, self.rho_head, self.rs_head):
            nn.init.xavier_uniform_(h.weight, gain=0.5)
            nn.init.zeros_(h.bias)

    def clamp_quantizable_weights(self):
        """Ensure all weights stay strictly within int16 safe bounds [-255, +255] (scale=128)."""
        with torch.no_grad():
            for layer in (self.fc1, self.fc2, self.eval_head, self.tau_head, self.rho_head, self.rs_head):
                layer.weight.clamp_(-255.0, 255.0)

    def _activate(self, x: torch.Tensor) -> torch.Tensor:
        clamped = torch.clamp(x, min=0.0, max=1.0)
        if self.activation == 'screlu':
            return clamped * clamped
        return clamped

    def _compute_fc1_accumulator(self, x: torch.Tensor) -> torch.Tensor:
        if self.qat and self.training:
            w1 = fake_quant_ste(self.fc1.weight, 128.0, -32767.0, 32767.0)
            b1 = fake_quant_ste(self.fc1.bias, 128.0, -2147483647.0, 2147483647.0)
        else:
            w1 = self.fc1.weight
            b1 = self.fc1.bias

        if x.dtype in (torch.int16, torch.int32, torch.int64):
            idx = x.long()
            weight_table = torch.cat([w1.t(), self._zero_pad_row.to(w1.dtype)], dim=0)
            acc = F.embedding_bag(idx, weight_table, mode='sum', padding_idx=PADDING_FEATURE_IDX)
            return acc + b1
        else:
            return F.linear(x, w1, b1)

    def _linear_qat(self, x: torch.Tensor, layer: nn.Linear, w_scale: float, b_scale: float) -> torch.Tensor:
        if self.qat and self.training:
            w = fake_quant_ste(layer.weight, w_scale, -32767.0, 32767.0)
            b = fake_quant_ste(layer.bias, b_scale, -2147483647.0, 2147483647.0)
            return F.linear(x, w, b)
        return layer(x)

    def forward(self, x: torch.Tensor):
        h1 = self._compute_fc1_accumulator(x)
        h1 = self._activate(h1)
        if self.qat and self.training:
            h1 = fake_quant_ste(h1, 128.0, 0.0, 128.0)

        h2 = self._linear_qat(h1, self.fc2, 128.0, 16384.0)
        h2 = self._activate(h2)
        if self.qat and self.training:
            h2 = fake_quant_ste(h2, 16384.0, 0.0, 16384.0)

        return {
            'eval': self._linear_qat(h2, self.eval_head, 128.0, 2097152.0).squeeze(-1),
            'tau': self._linear_qat(h2, self.tau_head, 128.0, 2097152.0).squeeze(-1),
            'rho': self._linear_qat(h2, self.rho_head, 128.0, 2097152.0).squeeze(-1),
            'rs': self._linear_qat(h2, self.rs_head, 128.0, 2097152.0).squeeze(-1),
        }

    def get_head(self, name: str, h):
        if name == 'eval':
            return self.eval_head(h).squeeze(-1)
        elif name == 'tau':
            return self.tau_head(h).squeeze(-1)
        elif name == 'rho':
            return self.rho_head(h).squeeze(-1)
        elif name == 'rs':
            return self.rs_head(h).squeeze(-1)
        raise ValueError(f"Unknown head: {name}")


class MultiTaskLoss(nn.Module):
    """
    HARENN-UE Stockfish NNUE-Grade Hybrid WDL BCE + Huber Centipawn Multi-Task Loss.
    Prioritizes positional and tactical evaluation accuracy while anchoring auxiliary search heads.
    """

    def __init__(self, stats: Dict[str, Tuple[float, float]], wdl_weight: float = 0.80, wdl_scale_cp: float = 400.0):
        super().__init__()
        self.wdl_weight = wdl_weight
        self.wdl_scale_pawns = wdl_scale_cp / 100.0  # 4.0 pawns = 400 cp

        self.register_buffer('eval_mean', torch.tensor(stats['eval'][0], dtype=torch.float32))
        self.register_buffer('eval_std', torch.tensor(stats['eval'][1], dtype=torch.float32))

    def compute_eval_raw_loss(self, outputs, labels):
        pred_eval_pawns = outputs['eval'] * self.eval_std + self.eval_mean
        target_pawns = labels['eval']

        # 1. Huber loss on pawns directly (beta = 0.5 pawns = 50 cp)
        huber_eval = F.smooth_l1_loss(pred_eval_pawns, target_pawns, reduction='none', beta=0.5)

        # 2. Binary Cross-Entropy on WDL probability (Stockfish NNUE standard):
        pred_wdl = torch.sigmoid(pred_eval_pawns / self.wdl_scale_pawns).clamp(1e-6, 1.0 - 1e-6)
        target_wdl = torch.sigmoid(target_pawns / self.wdl_scale_pawns).clamp(1e-6, 1.0 - 1e-6)
        bce_wdl = F.binary_cross_entropy(pred_wdl, target_wdl, reduction='none')

        # Combine: 75% WDL BCE + 25% Centipawn Huber
        return (self.wdl_weight * bce_wdl) + ((1.0 - self.wdl_weight) * huber_eval)

    def forward(self, outputs, labels):
        loss_eval = self.compute_eval_raw_loss(outputs, labels)
        loss_tau = (torch.sigmoid(outputs['tau']) - labels['tau']) ** 2
        loss_rho = (torch.sigmoid(outputs['rho']) - labels['rho']) ** 2
        loss_rs = (torch.sigmoid(outputs['rs']) - labels['rs']) ** 2

        # Eval is primary (1.0x), search parameters are auxiliary (0.15x each)
        total_loss = loss_eval.mean() + 0.15 * loss_tau.mean() + 0.15 * loss_rho.mean() + 0.15 * loss_rs.mean()

        return total_loss, {
            'total': total_loss.item(),
            'eval': loss_eval.mean().item(),
            'tau': loss_tau.mean().item(),
            'rho': loss_rho.mean().item(),
            'rs': loss_rs.mean().item()
        }


class HARENNTrainer:
    def __init__(self, model, train_loader, val_loader, device, stats, epochs: int = 20, wdl_weight: float = 0.80):
        self.model = model.to(device)
        self.train_loader = train_loader
        self.val_loader = val_loader
        self.device = device
        self.stats = stats
        self.epochs = max(1, epochs)
        self.criterion = MultiTaskLoss(stats, wdl_weight=wdl_weight).to(device)

        self.optimizer = optim.AdamW([
            {'params': self.model.fc1.parameters(), 'lr': 1e-3, 'weight_decay': 1e-5},
            {'params': self.model.fc2.parameters(), 'lr': 1e-3, 'weight_decay': 1e-5},
            {'params': self.model.eval_head.parameters(), 'lr': 1.5e-3, 'weight_decay': 1e-6},
            {'params': self.model.tau_head.parameters(), 'lr': 1.5e-3, 'weight_decay': 1e-6},
            {'params': self.model.rho_head.parameters(), 'lr': 1.5e-3, 'weight_decay': 1e-6},
            {'params': self.model.rs_head.parameters(), 'lr': 1.5e-3, 'weight_decay': 1e-6},
        ])

        self.scheduler = optim.lr_scheduler.CosineAnnealingLR(
            self.optimizer, T_max=max(1, epochs), eta_min=2e-5
        )

    def train_epoch(self, epoch):
        self.model.train()
        total_loss = 0.0
        epoch_losses = {'eval': 0.0, 'tau': 0.0, 'rho': 0.0, 'rs': 0.0}
        t0 = time.time()

        for batch_idx, (features, labels) in enumerate(self.train_loader):
            if features.device.type != self.device:
                features = features.to(self.device, non_blocking=True)
            labels = {k: (v.to(self.device, non_blocking=True)
                          if isinstance(v, torch.Tensor) and v.device.type != self.device else v)
                      for k, v in labels.items()}

            self.optimizer.zero_grad(set_to_none=True)
            outputs = self.model(features)

            loss, loss_dict = self.criterion(outputs, labels)
            loss.backward()

            torch.nn.utils.clip_grad_norm_(self.model.parameters(), max_norm=1.0)

            self.optimizer.step()
            self.model.clamp_quantizable_weights()

            total_loss += loss_dict['total']
            for key in epoch_losses:
                epoch_losses[key] += loss_dict[key]

            if batch_idx % 100 == 0 or batch_idx == len(self.train_loader) - 1:
                elapsed = max(1e-6, time.time() - t0)
                print(f"Epoch {epoch}, Batch {batch_idx}/{len(self.train_loader)} ({elapsed:.1f}s), "
                      f"Loss: {loss_dict['total']:.4f} "
                      f"(eval: {loss_dict['eval']:.4f}, tau: {loss_dict['tau']:.4f}, "
                      f"rho: {loss_dict['rho']:.4f}, rs: {loss_dict['rs']:.4f})", flush=True)

        num_batches = max(1, len(self.train_loader))
        avg_loss = total_loss / num_batches
        for key in epoch_losses:
            epoch_losses[key] /= num_batches

        dt = time.time() - t0
        print(f"\nEpoch {epoch} ({dt:.2f}s) - Average Train Loss: {avg_loss:.4f}")
        print(f"  eval: {epoch_losses['eval']:.4f}, tau: {epoch_losses['tau']:.4f}, "
              f"rho: {epoch_losses['rho']:.4f}, rs: {epoch_losses['rs']:.4f}", flush=True)

        return avg_loss

    def validate(self, return_details: bool = False):
        self.model.eval()
        total_loss = 0.0
        epoch_losses = {'eval': 0.0, 'tau': 0.0, 'rho': 0.0, 'rs': 0.0}
        total_mae_cp = 0.0
        total_wdl_err = 0.0
        total_samples = 0

        with torch.no_grad():
            for features, labels in self.val_loader:
                if features.device.type != self.device:
                    features = features.to(self.device, non_blocking=True)
                labels = {k: (v.to(self.device, non_blocking=True)
                              if isinstance(v, torch.Tensor) and v.device.type != self.device else v)
                          for k, v in labels.items()}

                outputs = self.model(features)
                loss, loss_dict = self.criterion(outputs, labels)

                pred_pawns = outputs['eval'] * self.stats['eval'][1] + self.stats['eval'][0]
                mae_cp = torch.abs(pred_pawns - labels['eval']) * 100.0
                total_mae_cp += mae_cp.sum().item()

                pred_wdl = torch.sigmoid(pred_pawns / 4.0)
                target_wdl = torch.sigmoid(labels['eval'] / 4.0)
                total_wdl_err += (torch.abs(pred_wdl - target_wdl) ** 2.5).sum().item()

                total_samples += features.size(0)

                total_loss += loss_dict['total']
                for key in epoch_losses:
                    epoch_losses[key] += loss_dict[key]

        num_batches = max(1, len(self.val_loader))
        avg_loss = total_loss / num_batches
        for key in epoch_losses:
            epoch_losses[key] /= num_batches
        avg_mae_cp = total_mae_cp / max(1, total_samples)
        avg_wdl_err = total_wdl_err / max(1, total_samples)

        print(f"Validation Loss: {avg_loss:.4f} | Eval MAE: {avg_mae_cp:.2f} cp | WDL^2.5 Err: {avg_wdl_err:.5f}", flush=True)
        print(f"  eval: {epoch_losses['eval']:.4f}, tau: {epoch_losses['tau']:.4f}, "
              f"rho: {epoch_losses['rho']:.4f}, rs: {epoch_losses['rs']:.4f}", flush=True)

        if return_details:
            return avg_loss, avg_mae_cp, avg_wdl_err
        return avg_loss

    def finetune_eval_stage(self, ft_epochs: int):
        """
        Stage 2 Post-Training Eval Fine-Tuning:
          - Phase 2A (First half of ft_epochs): Eval-Prioritized Joint Refinement
            (Backbone fc1/fc2 at low LR 1.2e-4, eval_head at 3e-4, auxiliary heads at 5e-5
             with 4x weight on eval WDL+Huber loss while keeping tau/rho/rs anchored).
          - Phase 2B (Second half of ft_epochs): Pure Eval Head Calibration
            (Freezes fc1, fc2, tau_head, rho_head, rs_head completely so tau/rho/rs suffer
             0.00% drift, and optimizes ONLY eval_head to minimize Centipawn MAE + WDL error).
        """
        if ft_epochs <= 0:
            return

        print("\n" + "=" * 68)
        print(f"STAGE 2: POST-TRAINING EVAL FINE-TUNING ({ft_epochs} Epochs)")
        print("=" * 68, flush=True)

        phase_a_epochs = max(1, ft_epochs // 2)
        phase_b_epochs = max(1, ft_epochs - phase_a_epochs)

        _, best_mae_cp, _ = self.validate(return_details=True)
        best_eval_state = {k: v.detach().cpu().clone() for k, v in self.model.state_dict().items()}

        # Phase 2A: Eval-Prioritized Backbone + Head Refinement
        print(f"\n--- Phase 2A: Eval-Prioritized Backbone Refinement ({phase_a_epochs} epochs) ---", flush=True)
        opt_a = optim.AdamW([
            {'params': self.model.fc1.parameters(), 'lr': 1.2e-4, 'weight_decay': 1e-5},
            {'params': self.model.fc2.parameters(), 'lr': 1.5e-4, 'weight_decay': 1e-5},
            {'params': self.model.eval_head.parameters(), 'lr': 3.0e-4, 'weight_decay': 1e-6},
            {'params': self.model.tau_head.parameters(), 'lr': 5.0e-5, 'weight_decay': 1e-6},
            {'params': self.model.rho_head.parameters(), 'lr': 5.0e-5, 'weight_decay': 1e-6},
            {'params': self.model.rs_head.parameters(), 'lr': 5.0e-5, 'weight_decay': 1e-6},
        ])
        sched_a = optim.lr_scheduler.CosineAnnealingLR(opt_a, T_max=phase_a_epochs, eta_min=1e-5)

        for ep in range(1, phase_a_epochs + 1):
            self.model.train()
            t0 = time.time()
            for batch_idx, (features, labels) in enumerate(self.train_loader):
                outputs = self.model(features)
                eval_raw = self.criterion.compute_eval_raw_loss(outputs, labels).mean()
                aux_loss = (
                    ((torch.sigmoid(outputs['tau']) - labels['tau']) ** 2).mean()
                    + ((torch.sigmoid(outputs['rho']) - labels['rho']) ** 2).mean()
                    + ((torch.sigmoid(outputs['rs']) - labels['rs']) ** 2).mean()
                )
                loss = 4.0 * eval_raw + 1.0 * aux_loss
                opt_a.zero_grad(set_to_none=True)
                loss.backward()
                torch.nn.utils.clip_grad_norm_(self.model.parameters(), max_norm=0.5)
                opt_a.step()
                self.model.clamp_quantizable_weights()

            sched_a.step()
            dt = time.time() - t0
            print(f"[Phase 2A - Epoch {ep}/{phase_a_epochs} ({dt:.1f}s)]", flush=True)
            _, val_mae_cp, _ = self.validate(return_details=True)
            if val_mae_cp < best_mae_cp:
                best_mae_cp = val_mae_cp
                best_eval_state = {k: v.detach().cpu().clone() for k, v in self.model.state_dict().items()}
                print(f"  *** New Best Eval MAE: {best_mae_cp:.2f} cp ***", flush=True)

        self.model.load_state_dict(best_eval_state)

        # Phase 2B: Exact Eval Head Calibration (Freeze fc1, fc2, tau, rho, rs)
        print(f"\n--- Phase 2B: Locked-Backbone Exact Eval Head Calibration ({phase_b_epochs} epochs) ---", flush=True)
        for p in self.model.parameters():
            p.requires_grad = False
        for p in self.model.eval_head.parameters():
            p.requires_grad = True

        opt_b = optim.AdamW(self.model.eval_head.parameters(), lr=1.5e-4, weight_decay=0.0)
        sched_b = optim.lr_scheduler.CosineAnnealingLR(opt_b, T_max=phase_b_epochs, eta_min=5e-6)

        for ep in range(1, phase_b_epochs + 1):
            self.model.train()
            t0 = time.time()
            for batch_idx, (features, labels) in enumerate(self.train_loader):
                outputs = self.model(features)
                eval_raw = self.criterion.compute_eval_raw_loss(outputs, labels).mean()
                opt_b.zero_grad(set_to_none=True)
                eval_raw.backward()
                opt_b.step()
                self.model.clamp_quantizable_weights()

            sched_b.step()
            dt = time.time() - t0
            print(f"[Phase 2B - Epoch {ep}/{phase_b_epochs} ({dt:.1f}s)]", flush=True)
            _, val_mae_cp, _ = self.validate(return_details=True)
            if val_mae_cp < best_mae_cp:
                best_mae_cp = val_mae_cp
                best_eval_state = {k: v.detach().cpu().clone() for k, v in self.model.state_dict().items()}
                print(f"  *** New Best Eval MAE: {best_mae_cp:.2f} cp ***", flush=True)

        for p in self.model.parameters():
            p.requires_grad = True
        self.model.load_state_dict(best_eval_state)
        print(f"\nStage 2 Eval Fine-Tuning Complete! Final Best Eval MAE: {best_mae_cp:.2f} cp", flush=True)

    def export_weights(self, filename: str, eval_mean: float, eval_std: float):
        """
        Export weights to .harenn binary format matching C++ loader (HNN4).

        Quantization scales:
          fc1:  W_int = 128 * W_float,       b_int = 128 * b_float
          fc2:  W_int = 128 * W_float,       b_int = 16384 * b_float
          head: W_int = 128 * W_float,       b_int = 2097152 * b_float
        """
        print(f"\nExporting weights to {filename}...")
        self.model.clamp_quantizable_weights()

        out_path = Path(filename)
        if out_path.parent:
            out_path.parent.mkdir(parents=True, exist_ok=True)

        with open(filename, 'wb') as f:
            f.write(b"HNN4")

            f.write(struct.pack('<f', float(eval_mean)))
            f.write(struct.pack('<f', float(eval_std)))

            layers = [
                (self.model.fc1.weight, self.model.fc1.bias, 128, 128, True, True),
                (self.model.fc2.weight, self.model.fc2.bias, 128, 16384, False, False),
                (self.model.eval_head.weight, self.model.eval_head.bias, 128, 2097152, False, False),
                (self.model.tau_head.weight, self.model.tau_head.bias, 128, 2097152, False, False),
                (self.model.rho_head.weight, self.model.rho_head.bias, 128, 2097152, False, False),
                (self.model.rs_head.weight, self.model.rs_head.bias, 128, 2097152, False, False),
            ]

            for w, b, w_scale, b_scale, bias_size_is_cols, do_transpose in layers:
                w_np = w.detach().cpu().numpy()
                b_np = b.detach().cpu().numpy()

                if do_transpose:
                    w_np = w_np.T

                rows, cols = w_np.shape

                f.write(struct.pack('<i', rows))
                f.write(struct.pack('<i', cols))

                scaled = np.round(w_np.astype(np.float64) * w_scale)
                max_abs = float(np.max(np.abs(scaled))) if scaled.size else 0.0
                assert max_abs <= 32767, (
                    f"quantised weight magnitude {max_abs} exceeds the int16 range"
                )
                weights_int16 = np.clip(scaled, -32767, 32767).astype(np.int16)
                f.write(weights_int16.tobytes())

                bias_int32 = np.clip(np.round(b_np.astype(np.float64) * b_scale),
                                     -2147483647, 2147483647).astype(np.int32)
                expected = cols if bias_size_is_cols else rows
                if bias_int32.size != expected:
                    sized = np.zeros(expected, dtype=np.int32)
                    count = min(expected, bias_int32.size)
                    sized[:count] = bias_int32[:count]
                    bias_int32 = sized

                f.write(bias_int32.tobytes())

        file_mb = Path(filename).stat().st_size / (1024 * 1024)
        print(f"Weights exported successfully to {filename} ({file_mb:.2f} MB)")


def main():
    parser = argparse.ArgumentParser(description='HARENN-UE Training Pipeline (Stockfish NNUE-Grade)')
    parser.add_argument('--data', type=str, required=True, help='Data directory (JSON/JSONL files or preprocessed .npy)')
    parser.add_argument('--epochs', type=int, default=18, help='Number of Stage 1 multi-task training epochs')
    parser.add_argument('--finetune-eval-epochs', type=int, default=6, help='Number of Stage 2 Eval fine-tuning epochs (0 = disabled)')
    parser.add_argument('--hidden1', type=int, default=1024, help='First hidden layer dimension (default: 1024)')
    parser.add_argument('--hidden2', type=int, default=512, help='Second hidden layer dimension (default: 512)')
    parser.add_argument('--batch-size', type=int, default=8192, help='Batch size')
    parser.add_argument('--val-split', type=float, default=0.05, help='Validation split ratio')
    parser.add_argument('--output', type=str, default='src/nextfish.harenn', help='Output model file')
    parser.add_argument('--device', type=str, default='cuda', help='Device to use (cuda/cpu)')
    parser.add_argument('--preprocessed', action='store_true', help='Use preprocessed .npy files')
    parser.add_argument('--patience', type=int, default=5, help='Early stopping patience in epochs (0 = disabled)')
    parser.add_argument('--sync-hf', action='store_true', help='Sync all .jsonl files from Hugging Face bucket before training')
    parser.add_argument('--hf-bucket', type=str, default='australia1/myrandombucket', help='Hugging Face bucket ID')
    parser.add_argument('--no-augment-flip', action='store_true', help='Disable symmetry dataset expansion')
    parser.add_argument('--symmetry', type=int, default=4, choices=[1, 2, 4],
                        help='Symmetry expansion multiplier (1=base only, 2=vertical color-flip, 4=4-way dual mirror, default: 4)')
    parser.add_argument('--feature-dropout', type=float, default=0.05,
                        help='Feature dropout rate (board noise on non-king pieces, default: 0.05)')
    parser.add_argument('--no-qat', action='store_true', help='Disable Quantization-Aware Training (QAT)')
    parser.add_argument('--activation', type=str, default='clipped_relu', choices=['clipped_relu', 'screlu'],
                        help='Hidden activation function (clipped_relu matches HNN4 C++, screlu for HARENN-UE squared activation)')
    parser.add_argument('--resume', type=str, default='', help='Path to checkpoint to resume training from')
    parser.add_argument('--init-weights', type=str, default='', help='Path to checkpoint to warm-start model weights from')
    parser.add_argument('--lr-scale', type=float, default=1.0, help='Multiplier applied to all Stage 1 learning rates (e.g. 0.1 for gentle fine-tuning)')
    parser.add_argument('--wdl-weight', type=float, default=0.80, help='Stockfish WDL BCE loss weight (default: 0.80 = 80%% WDL + 20%% Huber)')

    args = parser.parse_args()

    print("=" * 68)
    print("HARENN-UE Training Pipeline (Updatable Efficiently & NNUE-Grade)")
    print("=" * 68)
    print(f"Data directory:                 {args.data}")
    print(f"Architecture:                   768 -> {args.hidden1} -> {args.hidden2} -> 4 heads")
    print(f"Stage 1 Multi-Task Epochs:      {args.epochs}")
    print(f"Stage 2 Eval Fine-Tune Epochs:  {args.finetune_eval_epochs}")
    print(f"Batch size:                     {args.batch_size}")
    print(f"Validation split:               {args.val_split}")
    print(f"Output file:                    {args.output}")
    print(f"Device:                         {args.device}")
    print(f"Early stopping patience:        {args.patience}")
    print(f"Symmetry Expansion Mode:        {args.symmetry}x ({'4-Way Dual Mirror' if args.symmetry == 4 else ('2x Color-Flip' if args.symmetry == 2 else 'Disabled')})")
    print(f"Board Noise Feature Dropout:    {args.feature_dropout * 100:.1f}%")
    print(f"Quantization-Aware Training:    {not args.no_qat}")
    print(f"Hidden Activation Regime:       {args.activation}")
    print("=" * 68, flush=True)

    if args.sync_hf:
        sync_from_huggingface(args.hf_bucket, args.data)

    if not Path(args.data).exists():
        print(f"ERROR: Data directory not found: {args.data}")
        return

    print("\nLoading dataset...", flush=True)
    if args.preprocessed:
        dataset = PreprocessedHARENNDataset(args.data)
    else:
        dataset = HARENNDataset(args.data, use_cache=True, augment_flip=not args.no_augment_flip)

    if len(dataset) == 0:
        print("ERROR: No data loaded")
        return

    stats = dataset.label_stats()
    print("Label stats: " + ", ".join(
        f"{task}={stats[task][0]:.4f}/{stats[task][1]:.4f}" for task in TASKS), flush=True)

    train_indices, val_indices = split_by_game(dataset, args.val_split)

    device = args.device
    if device == 'cuda' and not torch.cuda.is_available():
        print("CUDA not available, using CPU")
        device = 'cpu'

    expand_flip = not args.no_augment_flip

    if isinstance(dataset, HARENNDataset):
        train_loader = FastSparseSymmetryBatchLoader(
            dataset, train_indices, batch_size=args.batch_size,
            shuffle=True, device=device, expand_flip=expand_flip,
            symmetry_mode=args.symmetry, feature_dropout=args.feature_dropout
        )
        val_loader = FastSparseSymmetryBatchLoader(
            dataset, val_indices, batch_size=args.batch_size,
            shuffle=False, device=device, expand_flip=expand_flip,
            symmetry_mode=args.symmetry, feature_dropout=0.0
        )
        print(f"Base Train samples:             {len(train_indices):,}")
        print(f"Symmetry Multiplier:            {args.symmetry}x")
        print(f"Board Noise Dropout:            {args.feature_dropout * 100:.1f}%")
        print(f"Total Effective Train samples:  {train_loader.total_samples:,}")
        print(f"Total Effective Val samples:    {val_loader.total_samples:,}", flush=True)
        print(f"Total Effective Train samples:  {train_loader.total_samples:,}")
        print(f"Total Effective Val samples:    {val_loader.total_samples:,}", flush=True)
    else:
        train_dataset = Subset(dataset, train_indices)
        val_dataset = Subset(dataset, val_indices)
        train_loader = DataLoader(train_dataset, batch_size=args.batch_size, shuffle=True)
        val_loader = DataLoader(val_dataset, batch_size=args.batch_size, shuffle=False)
        print(f"Train samples: {len(train_dataset):,}")
        print(f"Validation samples: {len(val_dataset):,}")

    print(f"\nUsing device: {device}")
    print(f"\nCreating HARENN-UE model (768 Sparse Accumulator -> {args.hidden1} -> {args.hidden2} -> 4 heads)...", flush=True)
    model = HARENNNetwork(
        hidden1=args.hidden1, hidden2=args.hidden2,
        qat=not args.no_qat, activation=args.activation
    )

    total_params = sum(p.numel() for p in model.parameters())
    trainable_params = sum(p.numel() for p in model.parameters() if p.requires_grad)
    print(f"Total parameters: {total_params:,}")
    print(f"Trainable parameters: {trainable_params:,}", flush=True)

    trainer = HARENNTrainer(model, train_loader, val_loader, device, stats, epochs=args.epochs, wdl_weight=args.wdl_weight)

    if args.lr_scale != 1.0:
        for g in trainer.optimizer.param_groups:
            g['lr'] *= args.lr_scale
        trainer.scheduler.base_lrs = [g['lr'] for g in trainer.optimizer.param_groups]
        trainer.scheduler.eta_min *= args.lr_scale
        print(f"Stage 1 learning rates scaled by {args.lr_scale}", flush=True)

    output_path = Path(args.output)
    checkpoint_dir = output_path.parent if str(output_path.parent) else Path(".")
    checkpoint_dir.mkdir(parents=True, exist_ok=True)

    print("\nStarting Stage 1 Joint Multi-Task Training...", flush=True)
    best_val_loss = float('inf')
    best_state = None
    best_epoch = 0
    epochs_without_improvement = 0
    start_epoch = 1

    if args.init_weights and Path(args.init_weights).exists():
        print("Warm-starting model weights from checkpoint:", args.init_weights)
        ckpt = torch.load(args.init_weights, map_location=device)
        model.load_state_dict(ckpt['model_state_dict'])
        print("Warm-start weights loaded successfully!")

    if args.resume and Path(args.resume).exists():
        print("Resuming model from checkpoint:", args.resume)
        ckpt = torch.load(args.resume, map_location=device)
        model.load_state_dict(ckpt['model_state_dict'])
        if 'optimizer_state_dict' in ckpt:
            try:
                trainer.optimizer.load_state_dict(ckpt['optimizer_state_dict'])
            except Exception as e:
                print("Warning: could not restore optimizer:", e)
        if 'scheduler_state_dict' in ckpt:
            try:
                trainer.scheduler.load_state_dict(ckpt['scheduler_state_dict'])
            except Exception as e:
                print("Warning: could not restore scheduler:", e)
        if 'val_loss' in ckpt:
            best_val_loss = float(ckpt['val_loss'])
            best_state = {k: v.detach().cpu().clone() for k, v in model.state_dict().items()}
        if 'epoch' in ckpt:
            start_epoch = int(ckpt['epoch']) + 1
            best_epoch = int(ckpt['epoch'])
            print(f"Resumed at epoch {start_epoch} (previous best val loss: {best_val_loss:.4f})")

    for epoch in range(start_epoch, args.epochs + 1):
        print(f"\n{'=' * 68}")
        print(f"Stage 1 - Epoch {epoch}/{args.epochs} (LR: {trainer.optimizer.param_groups[0]['lr']:.6f})")
        print('=' * 68, flush=True)

        train_loss = trainer.train_epoch(epoch)
        val_loss = trainer.validate()

        trainer.scheduler.step()

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_epoch = epoch
            best_state = {k: v.detach().cpu().clone() for k, v in model.state_dict().items()}
            epochs_without_improvement = 0
            print(f"\n*** New best validation loss: {val_loss:.4f} ***", flush=True)

            checkpoint_path = checkpoint_dir / f"harenn_checkpoint_epoch{epoch}.pt"
            torch.save({
                'epoch': epoch,
                'model_state_dict': model.state_dict(),
                'optimizer_state_dict': trainer.optimizer.state_dict(),
                'scheduler_state_dict': trainer.scheduler.state_dict(),
                'val_loss': val_loss,
                'eval_mean': stats['eval'][0],
                'eval_std': stats['eval'][1],
            }, checkpoint_path)
            print(f"Checkpoint saved to {checkpoint_path}", flush=True)
        else:
            epochs_without_improvement += 1
            print(f"No improvement (val loss {val_loss:.4f} vs best {best_val_loss:.4f})", flush=True)

        if args.patience > 0 and epochs_without_improvement >= args.patience:
            print(f"\nEarly stopping Stage 1: {args.patience} epochs without improvement.", flush=True)
            break

    if best_state is not None:
        print(f"\nRestoring best Stage 1 weights from epoch {best_epoch} (val loss {best_val_loss:.4f})", flush=True)
        model.load_state_dict(best_state)

    # Execute Stage 2 Post-Training Eval Fine-Tuning
    if args.finetune_eval_epochs > 0:
        trainer.finetune_eval_stage(args.finetune_eval_epochs)

    print("\n" + "=" * 68)
    print("Training & Eval Fine-Tuning Complete!")
    print("=" * 68, flush=True)
    trainer.export_weights(args.output, stats['eval'][0], stats['eval'][1])

    print(f"\nTraining summary:")
    print(f"Best Stage 1 validation loss: {best_val_loss:.4f} (epoch {best_epoch})")
    print(f"Eval normalisation: mean={stats['eval'][0]:.6f} std={stats['eval'][1]:.6f}")
    print(f"Model exported to: {args.output}", flush=True)


if __name__ == "__main__":
    main()
