import os
import sys
import time
import struct
import argparse
from pathlib import Path
from typing import Dict, Tuple

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim

NUM_FEATURES = 5632
PADDING_IDX = 5632
EVAL_SCALING = 4.0

class FastHKBBatchLoader:
    def __init__(self, us_data: np.ndarray, them_data: np.ndarray,
                 eval_data: np.ndarray, tau_data: np.ndarray,
                 rho_data: np.ndarray, rs_data: np.ndarray,
                 indices: np.ndarray, batch_size: int,
                 shuffle: bool = True, device: torch.device = torch.device('cuda')):
        self.us_data = us_data
        self.them_data = them_data
        self.eval_data = eval_data
        self.tau_data = tau_data
        self.rho_data = rho_data
        self.rs_data = rs_data
        self.indices = np.array(indices, dtype=np.int64)
        self.batch_size = batch_size
        self.shuffle = shuffle
        self.device = device
        self.num_samples = len(indices)
        self.num_batches = (self.num_samples + batch_size - 1) // batch_size

    def __len__(self):
        return self.num_batches

    def __iter__(self):
        if self.shuffle:
            np.random.shuffle(self.indices)
        self.batch_idx = 0
        return self

    def __next__(self):
        if self.batch_idx >= self.num_batches:
            raise StopIteration

        start = self.batch_idx * self.batch_size
        end = min(start + self.batch_size, self.num_samples)
        self.batch_idx += 1

        batch_idx = self.indices[start:end]

        us_batch = torch.from_numpy(self.us_data[batch_idx].astype(np.int64)).to(self.device, non_blocking=True)
        them_batch = torch.from_numpy(self.them_data[batch_idx].astype(np.int64)).to(self.device, non_blocking=True)

        target_eval = torch.from_numpy(self.eval_data[batch_idx]).to(self.device, non_blocking=True)
        target_tau = torch.from_numpy(self.tau_data[batch_idx]).to(self.device, non_blocking=True)
        target_rho = torch.from_numpy(self.rho_data[batch_idx]).to(self.device, non_blocking=True)
        target_rs = torch.from_numpy(self.rs_data[batch_idx]).to(self.device, non_blocking=True)

        targets = {
            'eval': target_eval,
            'tau': target_tau,
            'rho': target_rho,
            'rs': target_rs
        }
        return us_batch, them_batch, targets


class HKBNetwork(nn.Module):
    def __init__(self, hidden1: int = 512, hidden2: int = 512):
        super().__init__()
        self.hidden1 = hidden1
        self.hidden2 = hidden2

        self.embedding = nn.EmbeddingBag(
            num_embeddings=NUM_FEATURES + 1,
            embedding_dim=hidden1,
            mode='sum',
            sparse=False,
            padding_idx=PADDING_IDX
        )
        self.fc1_bias = nn.Parameter(torch.zeros(hidden1))
        self.fc2 = nn.Linear(hidden1 * 2, hidden2, bias=True)

        self.eval_head = nn.Linear(hidden2, 1, bias=True)
        self.tau_head = nn.Linear(hidden2, 1, bias=True)
        self.rho_head = nn.Linear(hidden2, 1, bias=True)
        self.rs_head = nn.Linear(hidden2, 1, bias=True)

        self._init_weights()

    def _init_weights(self):
        nn.init.kaiming_normal_(self.embedding.weight, nonlinearity='relu')
        self.embedding.weight.data.mul_(0.5)
        with torch.no_grad():
            self.embedding.weight.data[PADDING_IDX].zero_()
            self.fc1_bias.zero_()

        nn.init.kaiming_normal_(self.fc2.weight, nonlinearity='relu')
        self.fc2.weight.data.mul_(0.5)
        nn.init.zeros_(self.fc2.bias)

        for h in (self.eval_head, self.tau_head, self.rho_head, self.rs_head):
            nn.init.xavier_uniform_(h.weight, gain=0.5)
            nn.init.zeros_(h.bias)

    def clamp_weights(self):
        with torch.no_grad():
            self.embedding.weight.clamp_(-255.0, 255.0)
            self.embedding.weight.data[PADDING_IDX].zero_()
            self.fc1_bias.clamp_(-255.0, 255.0)
            self.fc2.weight.clamp_(-255.0, 255.0)
            for h in (self.eval_head, self.tau_head, self.rho_head, self.rs_head):
                h.weight.clamp_(-255.0, 255.0)

    def forward(self, us_indices: torch.Tensor, them_indices: torch.Tensor) -> Dict[str, torch.Tensor]:
        us_acc = self.embedding(us_indices) + self.fc1_bias
        them_acc = self.embedding(them_indices) + self.fc1_bias

        us_act = torch.clamp(us_acc, 0.0, 1.0)
        them_act = torch.clamp(them_acc, 0.0, 1.0)

        h1 = torch.cat([us_act, them_act], dim=-1)
        h2 = torch.clamp(self.fc2(h1), 0.0, 1.0)

        eval_score = self.eval_head(h2).squeeze(-1)
        tau_logit = self.tau_head(h2).squeeze(-1)
        rho_logit = self.rho_head(h2).squeeze(-1)
        rs_logit = self.rs_head(h2).squeeze(-1)

        return {'eval': eval_score, 'tau': tau_logit, 'rho': rho_logit, 'rs': rs_logit}


class MultiTaskLoss(nn.Module):
    def __init__(self, wdl_weight: float = 0.80):
        super().__init__()
        self.wdl_weight = wdl_weight
        self.huber = nn.HuberLoss(delta=1.0)

    def forward(self, pred: Dict[str, torch.Tensor], target: Dict[str, torch.Tensor]) -> Tuple[torch.Tensor, Dict[str, float]]:
        pred_pawns = pred['eval']
        target_pawns = target['eval']
        pred_cp = pred_pawns * 100.0
        target_cp = target_pawns * 100.0

        p_target = torch.sigmoid(target_pawns / EVAL_SCALING)
        loss_wdl = F.binary_cross_entropy_with_logits(pred_pawns / EVAL_SCALING, p_target)
        loss_huber = self.huber(pred_cp / 100.0, target_cp / 100.0)
        eval_loss = self.wdl_weight * loss_wdl + (1.0 - self.wdl_weight) * loss_huber

        tau_loss = F.binary_cross_entropy_with_logits(pred['tau'], target['tau'])
        rho_loss = F.binary_cross_entropy_with_logits(pred['rho'], target['rho'])
        rs_loss = F.binary_cross_entropy_with_logits(pred['rs'], target['rs'])

        total = eval_loss + 0.15 * tau_loss + 0.15 * rho_loss + 0.15 * rs_loss

        metrics = {
            'loss': float(total.item()),
            'eval_loss': float(eval_loss.item()),
            'loss_wdl': float(loss_wdl.item()),
            'loss_huber': float(loss_huber.item()),
            'mae_cp': float(torch.mean(torch.abs(pred_cp - target_cp)).item()),
            'tau_loss': float(tau_loss.item()),
            'rho_loss': float(rho_loss.item()),
            'rs_loss': float(rs_loss.item()),
        }
        return total, metrics


def export_hkb4(model: HKBNetwork, out_path: str, eval_mean: float = 0.0, eval_std: float = 1.0):
    model.eval()
    model.clamp_weights()
    with torch.no_grad():
        fc1_w = model.embedding.weight[:NUM_FEATURES].detach().cpu().numpy()  # (5632, 512)
        fc1_b = model.fc1_bias.detach().cpu().numpy()  # (512,)

        fc2_w = model.fc2.weight.detach().cpu().numpy()  # (512, 1024)
        fc2_b = model.fc2.bias.detach().cpu().numpy()  # (512,)

        fc1_w_q = np.clip(np.round(fc1_w * 128.0), -32767, 32767).astype(np.int16)
        fc1_b_q = np.clip(np.round(fc1_b * 128.0), -2147483647, 2147483647).astype(np.int32)

        fc2_w_q = np.clip(np.round(fc2_w * 128.0), -32767, 32767).astype(np.int16)
        fc2_b_q = np.clip(np.round(fc2_b * 16384.0), -2147483647, 2147483647).astype(np.int32)

        out_file = Path(out_path)
        out_file.parent.mkdir(parents=True, exist_ok=True)
        with open(out_file, 'wb') as f:
            f.write(b'HKB4')
            f.write(struct.pack('<ff', float(eval_mean), float(eval_std)))

            f.write(struct.pack('<ii', NUM_FEATURES, 512))
            f.write(fc1_w_q.tobytes())
            f.write(fc1_b_q.tobytes())

            f.write(struct.pack('<ii', 512, 1024))
            f.write(fc2_w_q.tobytes())
            f.write(fc2_b_q.tobytes())

            heads = [
                (model.eval_head, 128.0, 2097152.0),
                (model.tau_head, 128.0, 2097152.0),
                (model.rho_head, 128.0, 2097152.0),
                (model.rs_head, 128.0, 2097152.0),
            ]
            for h_layer, w_scale, b_scale in heads:
                hw = h_layer.weight.detach().cpu().numpy().squeeze(0)
                hb = float(h_layer.bias.item())
                hw_q = np.clip(np.round(hw * w_scale), -32767, 32767).astype(np.int16)
                hb_q = int(np.clip(np.round(hb * b_scale), -2147483647, 2147483647))

                f.write(struct.pack('<ii', 1, 512))
                f.write(hw_q.tobytes())
                f.write(struct.pack('<i', hb_q))

        sz_mb = out_file.stat().st_size / (1024 * 1024)
        print(f"[Export] Successfully exported HKB4 weights to {out_file} ({sz_mb:.2f} MB).", flush=True)


def train_main():
    parser = argparse.ArgumentParser(description="Train HARENN HalfKAv2 (King-Bucket) Multi-Head Network")
    parser.add_argument("--cache", type=str, default="Neuron Network/data/_hkb_sparse_cache_v1.npz")
    parser.add_argument("--epochs", type=int, default=14)
    parser.add_argument("--batch-size", type=int, default=8192)
    parser.add_argument("--lr", type=float, default=1.5e-3)
    parser.add_argument("--wdl-weight", type=float, default=0.80)
    parser.add_argument("--output", type=str, default="Neuron Network/nextfish_hkb.harenn")
    args = parser.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"[Train] Using device: {device} ({torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'CPU'})", flush=True)

    print(f"[Dataset] Loading HKB cache from {args.cache}...")
    t0 = time.time()
    cache_data = np.load(args.cache, allow_pickle=False)
    us_data = cache_data["us_indices"]
    them_data = cache_data["them_indices"]
    eval_data = cache_data["labels_eval"]
    tau_data = cache_data["labels_tau"]
    rho_data = cache_data["labels_rho"]
    rs_data = cache_data["labels_rs"]
    N = len(eval_data)
    eval_mean = float(np.mean(eval_data))
    eval_std = float(np.std(eval_data))
    print(f"[Dataset] Loaded {N:,} positions in {time.time() - t0:.2f}s (mean={eval_mean:.4f}, std={eval_std:.4f}).", flush=True)

    # Train / Val Split
    np.random.seed(42)
    val_count = min(50000, int(N * 0.05))
    all_perm = np.random.permutation(N)
    val_indices = all_perm[:val_count]
    train_indices = all_perm[val_count:]

    train_loader = FastHKBBatchLoader(
        us_data, them_data, eval_data, tau_data, rho_data, rs_data,
        train_indices, args.batch_size, shuffle=True, device=device
    )
    val_loader = FastHKBBatchLoader(
        us_data, them_data, eval_data, tau_data, rho_data, rs_data,
        val_indices, args.batch_size, shuffle=False, device=device
    )

    model = HKBNetwork(hidden1=512, hidden2=512).to(device)
    criterion = MultiTaskLoss(wdl_weight=args.wdl_weight).to(device)
    optimizer = optim.AdamW([
        {'params': model.embedding.parameters(), 'lr': args.lr, 'weight_decay': 1e-5},
        {'params': model.fc1_bias, 'lr': args.lr, 'weight_decay': 1e-5},
        {'params': model.fc2.parameters(), 'lr': args.lr, 'weight_decay': 1e-5},
        {'params': model.eval_head.parameters(), 'lr': args.lr * 1.5, 'weight_decay': 1e-6},
        {'params': model.tau_head.parameters(), 'lr': args.lr * 1.5, 'weight_decay': 1e-6},
        {'params': model.rho_head.parameters(), 'lr': args.lr * 1.5, 'weight_decay': 1e-6},
        {'params': model.rs_head.parameters(), 'lr': args.lr * 1.5, 'weight_decay': 1e-6},
    ])
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs, eta_min=2e-5)

    best_val_mae = 9999.0

    print(f"[Train] Starting Fast Vectorized HalfKAv2 (King-Bucket) training for {args.epochs} epochs (Batch: {args.batch_size}, WDL: {args.wdl_weight:.2f})...", flush=True)
    for epoch in range(1, args.epochs + 1):
        model.train()
        t0 = time.time()
        total_loss = 0.0
        total_mae = 0.0
        batches = 0

        for us, them, targets in train_loader:
            optimizer.zero_grad(set_to_none=True)
            with torch.amp.autocast('cuda', enabled=(device.type == 'cuda')):
                outputs = model(us, them)
                loss, metrics = criterion(outputs, targets)

            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()
            model.clamp_weights()

            total_loss += metrics['loss']
            total_mae += metrics['mae_cp']
            batches += 1

        scheduler.step()
        train_time = time.time() - t0
        avg_loss = total_loss / batches
        avg_mae = total_mae / batches

        # Fast Validation
        model.eval()
        val_loss = 0.0
        val_mae = 0.0
        val_batches = 0
        with torch.no_grad():
            for us, them, targets in val_loader:
                with torch.amp.autocast('cuda', enabled=(device.type == 'cuda')):
                    outputs = model(us, them)
                    _, metrics = criterion(outputs, targets)

                val_loss += metrics['loss']
                val_mae += metrics['mae_cp']
                val_batches += 1

        avg_val_loss = val_loss / val_batches
        avg_val_mae = val_mae / val_batches

        print(f"Epoch {epoch:2d}/{args.epochs:2d} | Train Loss: {avg_loss:.4f} MAE: {avg_mae:.1f}cp | Val Loss: {avg_val_loss:.4f} MAE: {avg_val_mae:.1f}cp | LR: {scheduler.get_last_lr()[0]:.2e} | Time: {train_time:.1f}s", flush=True)

        if avg_val_mae < best_val_mae:
            best_val_mae = avg_val_mae
            export_hkb4(model, args.output, eval_mean=0.0, eval_std=1.0)

    print(f"[Train] Training complete! Best Validation MAE: {best_val_mae:.1f} centipawns.", flush=True)


if __name__ == '__main__':
    train_main()
