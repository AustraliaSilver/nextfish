#!/usr/bin/env python3
"""
HARENN Training Pipeline
Multi-head NNUE training for Horizon-Aware Reduction
Network: 768-dim piece-square input → 512 → 256 → 4 heads (eval, tau, rho, rs)
Architecture matches C++ inference engine.

Usage:
    python train_harenn.py --data data --epochs 20 --batch-size 4096
"""

import argparse
import json
import math
import os
import random
import struct
import time
from pathlib import Path
from typing import List, Dict, Tuple, Optional
import numpy as np
import torch
import torch.nn as nn
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


def fen_side_to_move(fen: str) -> int:
    """0 = white to move, 1 = black to move."""
    parts = fen.split()
    if len(parts) < 2:
        raise ValueError(f"Malformed FEN: {fen!r}")
    return 0 if parts[1] == 'w' else 1


def fen_move_number(fen: str) -> Optional[int]:
    parts = fen.split()
    if len(parts) < 6:
        return None
    try:
        return int(parts[5])
    except ValueError:
        return None


def game_id_from_name(stem: str) -> str:
    """Derive a game identifier from a data file name (w0_g7_b3_... -> g7)."""
    parts = stem.split('_')
    for part in parts:
        if part.startswith('g') and part[1:].isdigit():
            return f"g{part[1:]}"
    return stem


def fen_to_features(fen: str) -> np.ndarray:
    """
    Convert FEN to 768-dim piece-square feature vector.
    Encoding matches C++ extract_features:
      py_sq = sq ^ 56 (rank flip a1<->a8, bottom-up to top-down)
      py_pc = piece - 1  (W:0-5, B:6-11)
      feat  = py_sq * 12 + py_pc
    """
    board_part = fen.split()[0]

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
    """HARENN corpus loaded from *.json ({"positions": [...]}) and *.jsonl files."""

    def __init__(self, data_dir: str):
        self.records: List[Dict[str, float]] = []
        self.game_ids: List[str] = []
        self._load_data(data_dir)

    def _load_data(self, data_dir: str):
        data_path = Path(data_dir)
        if not data_path.exists():
            raise FileNotFoundError(f"Data directory not found: {data_dir}")

        json_files = sorted(data_path.glob("*.json"))
        jsonl_files = sorted(data_path.glob("*.jsonl"))
        total_positions = 0
        for json_file in json_files:
            total_positions += self._load_json_file(json_file)
        for jsonl_file in jsonl_files:
            total_positions += self._load_jsonl_file(jsonl_file)

        print(f"Loaded {total_positions} positions from {len(json_files) + len(jsonl_files)} files "
              f"({len(json_files)} json, {len(jsonl_files)} jsonl)")

    def _load_json_file(self, json_file: Path) -> int:
        try:
            with open(json_file, 'r') as f:
                file_data = json.load(f)
        except json.JSONDecodeError as e:
            raise ValueError(f"{json_file}: invalid JSON: {e}") from e

        if not isinstance(file_data, dict) or 'positions' not in file_data:
            raise ValueError(f"{json_file}: expected a JSON object containing a 'positions' list")

        game_id = game_id_from_name(json_file.stem)
        for index, pos in enumerate(file_data['positions']):
            self._add_record(pos, game_id, f"{json_file}[{index}]")
        return len(file_data['positions'])

    def _load_jsonl_file(self, jsonl_file: Path) -> int:
        game_id = jsonl_file.stem
        game_index = 0
        prev_move_number = None
        count = 0
        with open(jsonl_file, 'r') as f:
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

                self._add_record(pos, f"{game_id}#{game_index}", f"{jsonl_file}:{lineno}")
                count += 1
        return count

    def _add_record(self, pos, game_id: str, where: str):
        if not isinstance(pos, dict):
            raise ValueError(f"{where}: expected a JSON object")
        missing = [key for key in REQUIRED_KEYS if key not in pos]
        if missing:
            raise ValueError(f"{where}: missing required field(s): {', '.join(missing)}")

        fen = pos['fen']
        if 'stm' in pos and pos['stm'] is not None:
            stm = int(pos['stm'])
        else:
            stm = fen_side_to_move(fen)

        eval_score = float(pos['eval_score'])
        if stm == 1:
            eval_score = -eval_score

        self.records.append({
            'fen': fen,
            'eval': eval_score / 100.0,
            'tau': float(pos['tau']),
            'rho': float(pos['rho']),
            'rs': float(pos['rs']),
        })
        self.game_ids.append(game_id)

    def __len__(self):
        return len(self.records)

    def __getitem__(self, idx):
        rec = self.records[idx]
        features = fen_to_features(rec['fen'])
        labels = {task: torch.tensor(rec[task], dtype=torch.float32) for task in TASKS}
        return torch.tensor(features, dtype=torch.float32), labels

    def label_stats(self) -> Dict[str, Tuple[float, float]]:
        """Per-task (mean, std) of the raw targets plus the initial variance."""
        stats = {}
        for task in TASKS:
            values = np.array([rec[task] for rec in self.records], dtype=np.float64)
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
    random.Random(seed).shuffle(unique_games)
    val_games = set(unique_games[:max(1, int(round(len(unique_games) * val_split)))])
    train_indices = [i for i, gid in enumerate(game_ids) if gid not in val_games]
    val_indices = [i for i, gid in enumerate(game_ids) if gid in val_games]
    return train_indices, val_indices


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
        # The C++ inference clamps fc1 to [0,128] and fc2 to [0,16384], i.e. [0,1]
        h = torch.clamp(self.fc1(x), min=0.0, max=1.0)
        h = torch.clamp(self.fc2(h), min=0.0, max=1.0)

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
    """Per-task uncertainty weighted loss on normalised targets."""

    def __init__(self, stats: Dict[str, Tuple[float, float]]):
        super().__init__()
        self.register_buffer('eval_mean', torch.tensor(stats['eval'][0], dtype=torch.float32))
        self.register_buffer('eval_std', torch.tensor(stats['eval'][1], dtype=torch.float32))
        # the eval target is normalised, so its initial variance is 1
        self.log_var_eval = nn.Parameter(torch.tensor([math.log(1.0)]))
        self.log_var_tau = nn.Parameter(torch.tensor([math.log(max(stats['tau'][1] ** 2, 1e-6))]))
        self.log_var_rho = nn.Parameter(torch.tensor([math.log(max(stats['rho'][1] ** 2, 1e-6))]))
        self.log_var_rs = nn.Parameter(torch.tensor([math.log(max(stats['rs'][1] ** 2, 1e-6))]))

    def forward(self, outputs, labels):
        eval_target = (labels['eval'] - self.eval_mean) / self.eval_std
        loss_eval = ((outputs['eval'] - eval_target) ** 2) / torch.exp(self.log_var_eval) + self.log_var_eval
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
    def __init__(self, model, train_loader, val_loader, device, stats):
        self.model = model.to(device)
        self.train_loader = train_loader
        self.val_loader = val_loader
        self.device = device
        self.stats = stats
        self.criterion = MultiTaskLoss(stats).to(device)

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

    def export_weights(self, filename: str, eval_mean: float, eval_std: float):
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

        print(f"Weights exported successfully to {filename}")


def main():
    parser = argparse.ArgumentParser(description='HARENN Training Pipeline')
    parser.add_argument('--data', type=str, required=True, help='Data directory (JSON/JSONL files or preprocessed .npy)')
    parser.add_argument('--epochs', type=int, default=20, help='Number of training epochs')
    parser.add_argument('--batch-size', type=int, default=4096, help='Batch size')
    parser.add_argument('--val-split', type=float, default=0.1, help='Validation split ratio')
    parser.add_argument('--output', type=str, default='src/nextfish.harenn', help='Output model file')
    parser.add_argument('--device', type=str, default='cuda', help='Device to use (cuda/cpu)')
    parser.add_argument('--preprocessed', action='store_true', help='Use preprocessed .npy files')
    parser.add_argument('--patience', type=int, default=0, help='Early stopping patience in epochs (0 = disabled)')

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
    print(f"Early stopping patience: {args.patience}")
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

    stats = dataset.label_stats()
    print(f"Label stats: " + ", ".join(
        f"{task}={stats[task][0]:.4f}/{stats[task][1]:.4f}" for task in TASKS))

    train_indices, val_indices = split_by_game(dataset, args.val_split)
    train_dataset = Subset(dataset, train_indices)
    val_dataset = Subset(dataset, val_indices)

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

    trainer = HARENNTrainer(model, train_loader, val_loader, device, stats)

    output_path = Path(args.output)
    checkpoint_dir = output_path.parent if str(output_path.parent) else Path(".")
    checkpoint_dir.mkdir(parents=True, exist_ok=True)

    print("\nStarting training...")
    best_val_loss = float('inf')
    best_state = None
    best_epoch = 0
    epochs_without_improvement = 0

    for epoch in range(1, args.epochs + 1):
        print(f"\n{'=' * 60}")
        print(f"Epoch {epoch}/{args.epochs}")
        print('=' * 60)

        train_loss = trainer.train_epoch(epoch)
        val_loss = trainer.validate()

        trainer.scheduler.step(val_loss)

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_epoch = epoch
            best_state = {k: v.detach().cpu().clone() for k, v in model.state_dict().items()}
            epochs_without_improvement = 0
            print(f"\n*** New best validation loss: {val_loss:.4f} ***")

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
            print(f"Checkpoint saved to {checkpoint_path}")
        else:
            epochs_without_improvement += 1
            print(f"No improvement (val loss {val_loss:.4f} vs best {best_val_loss:.4f})")

        if args.patience > 0 and epochs_without_improvement >= args.patience:
            print(f"\nEarly stopping: {args.patience} epochs without improvement.")
            break

    if best_state is not None:
        print(f"\nRestoring best weights from epoch {best_epoch} (val loss {best_val_loss:.4f})")
        model.load_state_dict(best_state)

    print("\n" + "=" * 60)
    print("Training complete!")
    print("=" * 60)
    trainer.export_weights(args.output, stats['eval'][0], stats['eval'][1])

    print(f"\nTraining summary:")
    print(f"Best validation loss: {best_val_loss:.4f} (epoch {best_epoch})")
    print(f"Eval normalisation: mean={stats['eval'][0]:.6f} std={stats['eval'][1]:.6f}")
    print(f"Model exported to: {args.output}")


if __name__ == "__main__":
    main()
