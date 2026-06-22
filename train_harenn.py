#!/usr/bin/env python3
"""
HARENN Training Pipeline
Multi-head NNUE training for Horizon-Aware Reduction
Network: 768-dim piece-square input → 512 → 256 → 4 heads (eval, tau, rho, rs)
Architecture matches C++ inference engine.

Usage:
    python train_harenn.py --data D:/nextfish/backup_harenn_data --epochs 20 --batch-size 4096
"""

import argparse
import os
import json
import time
import struct
import random
from pathlib import Path
from typing import List, Dict, Tuple, Optional
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader

torch.manual_seed(42)
np.random.seed(42)
random.seed(42)

PIECE_MAP = {
    'P': 0, 'N': 1, 'B': 2, 'R': 3, 'Q': 4, 'K': 5,
    'p': 6, 'n': 7, 'b': 8, 'r': 9, 'q': 10, 'k': 11
}


def fen_to_features(fen: str, stm: int) -> np.ndarray:
    """
    Convert FEN to 768-dim piece-square feature vector.
    Encoding matches C++ extract_features:
      py_sq = sq ^ 56 (rank flip a1<->a8, bottom-up to top-down)
      py_pc = piece - 1  (W:0-5, B:6-11)
      feat  = py_sq * 12 + py_pc
    """
    board_part, side_part = fen.split()[:2]

    features = np.zeros(768, dtype=np.float32)
    squares = board_part.split('/')
    for rank_fen, row in enumerate(squares):
        file = 0
        for ch in row:
            if ch.isdigit():
                file += int(ch)
            else:
                sf_sq = (7 - rank_fen) * 8 + file
                py_sq = sf_sq ^ 56
                py_pc = PIECE_MAP[ch]
                idx = py_sq * 12 + py_pc
                features[idx] = 1.0
                file += 1

    return features


class HARENNDataset(Dataset):
    def __init__(self, data_dir: str):
        self.data = []
        self._load_data(data_dir)

    def _load_data(self, data_dir: str):
        data_path = Path(data_dir)
        if not data_path.exists():
            raise FileNotFoundError(f"Data directory not found: {data_dir}")

        json_files = list(data_path.glob("*.json"))
        print(f"Loading data from {len(json_files)} files...")

        total_positions = 0
        for json_file in json_files:
            try:
                with open(json_file, 'r') as f:
                    file_data = json.load(f)
                    if 'positions' in file_data:
                        for pos_data in file_data['positions']:
                            self.data.append(pos_data)
                            total_positions += 1
            except Exception as e:
                print(f"Error loading {json_file}: {e}")

        print(f"Loaded {total_positions} positions from {len(json_files)} files")

    def __len__(self):
        return len(self.data)

    def __getitem__(self, idx):
        pos = self.data[idx]
        features = fen_to_features(pos['fen'], pos['stm'])

        labels = {
            'eval': torch.tensor(pos['eval_score'] / 100.0, dtype=torch.float32),
            'tau': torch.tensor(pos['tau'], dtype=torch.float32),
            'rho': torch.tensor(pos['rho'], dtype=torch.float32),
            'rs': torch.tensor(pos['rs'], dtype=torch.float32)
        }

        return torch.tensor(features, dtype=torch.float32), labels


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


class HARENNNetwork(nn.Module):
    """
    HARENN: 768-dim piece-square input → 512 → 256 → 4 heads
    Matches the C++ inference engine architecture.
    """

    def __init__(self):
        super().__init__()

        self.fc1 = nn.Linear(768, 512, bias=True)
        self.fc2 = nn.Linear(512, 256, bias=True)

        self.eval_head = nn.Linear(256, 1, bias=True)
        self.tau_head = nn.Linear(256, 1, bias=True)
        self.rho_head = nn.Linear(256, 1, bias=True)
        self.rs_head = nn.Linear(256, 1, bias=True)

    def forward(self, x):
        h = torch.clamp(self.fc1(x), min=0.0)
        h = torch.clamp(self.fc2(h), min=0.0)

        return {
            'eval': self.eval_head(h).squeeze(-1),
            'tau': self.tau_head(h).squeeze(-1),
            'rho': self.rho_head(h).squeeze(-1),
            'rs': self.rs_head(h).squeeze(-1)
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
    def __init__(self):
        super().__init__()
        self.log_var_eval = nn.Parameter(torch.zeros(1))
        self.log_var_tau = nn.Parameter(torch.zeros(1))
        self.log_var_rho = nn.Parameter(torch.zeros(1))
        self.log_var_rs = nn.Parameter(torch.zeros(1))

    def forward(self, outputs, labels):
        loss_eval = ((outputs['eval'] - labels['eval']) ** 2) / torch.exp(self.log_var_eval) + self.log_var_eval
        loss_tau = ((torch.sigmoid(outputs['tau']) - labels['tau']) ** 2) / torch.exp(self.log_var_tau) + self.log_var_tau
        loss_rho = ((torch.sigmoid(outputs['rho']) - labels['rho']) ** 2) / torch.exp(self.log_var_rho) + self.log_var_rho
        loss_rs = ((torch.sigmoid(outputs['rs']) - labels['rs']) ** 2) / torch.exp(self.log_var_rs) + self.log_var_rs

        total_loss = loss_eval.mean() + loss_tau.mean() + loss_rho.mean() + loss_rs.mean()

        return total_loss, {
            'total': total_loss.item(),
            'eval': loss_eval.mean().item(),
            'tau': loss_tau.mean().item(),
            'rho': loss_rho.mean().item(),
            'rs': loss_rs.mean().item()
        }


class HARENNTrainer:
    def __init__(self, model, train_loader, val_loader, device):
        self.model = model.to(device)
        self.train_loader = train_loader
        self.val_loader = val_loader
        self.device = device
        self.criterion = MultiTaskLoss().to(device)

        self.optimizer = optim.Adam([
            {'params': self.model.fc1.parameters(), 'lr': 1e-4},
            {'params': self.model.fc2.parameters(), 'lr': 3e-4},
            {'params': self.model.eval_head.parameters(), 'lr': 5e-4},
            {'params': self.model.tau_head.parameters(), 'lr': 1e-3},
            {'params': self.model.rho_head.parameters(), 'lr': 1e-3},
            {'params': self.model.rs_head.parameters(), 'lr': 1e-3},
            {'params': self.criterion.parameters(), 'lr': 1e-3},
        ])

        self.scheduler = optim.lr_scheduler.ReduceLROnPlateau(
            self.optimizer, mode='min', factor=0.5, patience=3
        )

    def train_epoch(self, epoch):
        self.model.train()
        total_loss = 0
        epoch_losses = {'eval': 0, 'tau': 0, 'rho': 0, 'rs': 0}

        for batch_idx, (features, labels) in enumerate(self.train_loader):
            features = features.to(self.device)
            labels = {k: v.to(self.device) if isinstance(v, torch.Tensor) else v
                      for k, v in labels.items()}

            self.optimizer.zero_grad()
            outputs = self.model(features)

            loss, loss_dict = self.criterion(outputs, labels)
            loss.backward()

            torch.nn.utils.clip_grad_norm_(self.model.parameters(), max_norm=1.0)

            self.optimizer.step()

            total_loss += loss_dict['total']
            for key in epoch_losses:
                epoch_losses[key] += loss_dict[key]

            if batch_idx % 50 == 0:
                print(f"Epoch {epoch}, Batch {batch_idx}/{len(self.train_loader)}, "
                      f"Loss: {loss_dict['total']:.4f} "
                      f"(eval: {loss_dict['eval']:.4f}, tau: {loss_dict['tau']:.4f}, "
                      f"rho: {loss_dict['rho']:.4f}, rs: {loss_dict['rs']:.4f})")

        avg_loss = total_loss / len(self.train_loader)
        for key in epoch_losses:
            epoch_losses[key] /= len(self.train_loader)

        print(f"\nEpoch {epoch} - Average Train Loss: {avg_loss:.4f}")
        print(f"  eval: {epoch_losses['eval']:.4f}, tau: {epoch_losses['tau']:.4f}, "
              f"rho: {epoch_losses['rho']:.4f}, rs: {epoch_losses['rs']:.4f}")

        return avg_loss

    def validate(self):
        self.model.eval()
        total_loss = 0
        epoch_losses = {'eval': 0, 'tau': 0, 'rho': 0, 'rs': 0}

        with torch.no_grad():
            for features, labels in self.val_loader:
                features = features.to(self.device)
                labels = {k: v.to(self.device) if isinstance(v, torch.Tensor) else v
                          for k, v in labels.items()}

                outputs = self.model(features)
                loss, loss_dict = self.criterion(outputs, labels)

                total_loss += loss_dict['total']
                for key in epoch_losses:
                    epoch_losses[key] += loss_dict[key]

        avg_loss = total_loss / len(self.val_loader)
        for key in epoch_losses:
            epoch_losses[key] /= len(self.val_loader)

        print(f"Validation Loss: {avg_loss:.4f}")
        print(f"  eval: {epoch_losses['eval']:.4f}, tau: {epoch_losses['tau']:.4f}, "
              f"rho: {epoch_losses['rho']:.4f}, rs: {epoch_losses['rs']:.4f}")

        return avg_loss

    def export_weights(self, filename: str):
        """
        Export weights to .harenn binary format matching C++ loader.

        Quantization scales:
          fc1:  W_int = 128 * W_float,       b_int = 128 * b_float
          fc2:  W_int = 128 * W_float,       b_int = 16384 * b_float
          head: W_int = 128 * W_float,       b_int = 2097152 * b_float

        Binary layout:
          [0:4]   magic "HNN4"
          [4:8]   eval_mean (float32)
          [8:12]  eval_std (float32)
          [12:..] layers (fc1, fc2, eval_head, tau_head, rho_head, rs_head)
          each layer: rows(int32) cols(int32) weights(int16[]) bias(int32[])
        """
        print(f"\nExporting weights to {filename}...")

        with open(filename, 'wb') as f:
            f.write(b"HNN4")

            eval_mean = 0.0
            eval_std = 1.0
            f.write(struct.pack('<f', eval_mean))
            f.write(struct.pack('<f', eval_std))

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

                weights_int16 = (w_np * w_scale).astype(np.int16)
                f.write(weights_int16.tobytes())

                if bias_size_is_cols:
                    bias_int32 = (b_np * b_scale).astype(np.int32)
                else:
                    bias_int32 = (b_np * b_scale).astype(np.int32)

                f.write(bias_int32.tobytes())

        print(f"Weights exported successfully to {filename}")


def main():
    parser = argparse.ArgumentParser(description='HARENN Training Pipeline')
    parser.add_argument('--data', type=str, required=True, help='Data directory (JSON files or preprocessed .npy)')
    parser.add_argument('--epochs', type=int, default=20, help='Number of training epochs')
    parser.add_argument('--batch-size', type=int, default=4096, help='Batch size')
    parser.add_argument('--val-split', type=float, default=0.1, help='Validation split ratio')
    parser.add_argument('--output', type=str, default='src/nextfish.harenn', help='Output model file')
    parser.add_argument('--device', type=str, default='cuda', help='Device to use (cuda/cpu)')
    parser.add_argument('--preprocessed', action='store_true', help='Use preprocessed .npy files')

    args = parser.parse_args()

    print("=" * 60)
    print("HARENN Training Pipeline")
    print("=" * 60)
    print(f"Data directory: {args.data}")
    print(f"Epochs: {args.epochs}")
    print(f"Batch size: {args.batch_size}")
    print(f"Validation split: {args.val_split}")
    print(f"Output file: {args.output}")
    print(f"Device: {args.device}")
    print("=" * 60)

    if not Path(args.data).exists():
        print(f"ERROR: Data directory not found: {args.data}")
        return

    print("\nLoading dataset...")
    if args.preprocessed:
        dataset = PreprocessedHARENNDataset(args.data)
    else:
        dataset = HARENNDataset(args.data)

    if len(dataset) == 0:
        print("ERROR: No data loaded")
        return

    train_size = int((1 - args.val_split) * len(dataset))
    val_size = len(dataset) - train_size

    train_dataset, val_dataset = torch.utils.data.random_split(
        dataset, [train_size, val_size],
        generator=torch.Generator().manual_seed(42)
    )

    print(f"Train samples: {len(train_dataset)}")
    print(f"Validation samples: {len(val_dataset)}")

    train_loader = DataLoader(
        train_dataset,
        batch_size=args.batch_size,
        shuffle=True,
        num_workers=0,
        pin_memory=False
    )

    val_loader = DataLoader(
        val_dataset,
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=0,
        pin_memory=False
    )

    device = args.device
    if device == 'cuda' and not torch.cuda.is_available():
        print("CUDA not available, using CPU")
        device = 'cpu'

    print(f"\nUsing device: {device}")

    print("\nCreating HARENN model (768-dim piece-square -> 512 -> 256 -> 4 heads)...")
    model = HARENNNetwork()

    total_params = sum(p.numel() for p in model.parameters())
    trainable_params = sum(p.numel() for p in model.parameters() if p.requires_grad)
    print(f"Total parameters: {total_params:,}")
    print(f"Trainable parameters: {trainable_params:,}")

    trainer = HARENNTrainer(model, train_loader, val_loader, device)

    print("\nStarting training...")
    best_val_loss = float('inf')

    for epoch in range(1, args.epochs + 1):
        print(f"\n{'=' * 60}")
        print(f"Epoch {epoch}/{args.epochs}")
        print(f"{'=' * 60}")

        train_loss = trainer.train_epoch(epoch)
        val_loss = trainer.validate()

        trainer.scheduler.step(val_loss)

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            print(f"\n*** New best validation loss: {val_loss:.4f} ***")

            checkpoint_path = f"harenn_checkpoint_epoch{epoch}.pt"
            torch.save({
                'epoch': epoch,
                'model_state_dict': model.state_dict(),
                'optimizer_state_dict': trainer.optimizer.state_dict(),
                'scheduler_state_dict': trainer.scheduler.state_dict(),
                'val_loss': val_loss,
            }, checkpoint_path)
            print(f"Checkpoint saved to {checkpoint_path}")

    print("\n" + "=" * 60)
    print("Training complete!")
    print("=" * 60)
    trainer.export_weights(args.output)

    print(f"\nTraining summary:")
    print(f"Best validation loss: {best_val_loss:.4f}")
    print(f"Model exported to: {args.output}")


if __name__ == "__main__":
    main()
