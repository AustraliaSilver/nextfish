#!/usr/bin/env python3
"""
HARENN Turbo Parallel Data Generator - V8 (Raw Data Collection)
D:/nextfish/data - Loại bỏ mọi bộ lọc, phân tích 100% nước đi
"""

import argparse
import os
import sys
import json
import time
import random
import logging
from pathlib import Path
from dataclasses import dataclass, asdict
import chess
import chess.engine
from concurrent.futures import ProcessPoolExecutor, as_completed
import multiprocessing

logging.getLogger("chess.engine").setLevel(logging.CRITICAL)

@dataclass
class TrainingPosition:
    fen: str
    stm: int
    eval_score: int
    depth: int
    best_move: str
    best_move_label: int
    best_moves_d16: list
    best_moves_d20: list
    best_moves_d24: list
    best_move_labels_d16: list
    best_move_labels_d20: list
    best_move_labels_d24: list
    game_result: int
    material: int
    piece_count: int
    tau: float
    rho: float
    rs: float

class ParallelGenerator:
    def __init__(self, engine_path, output_dir, depth=16):
        self.engine_path = engine_path
        self.output_dir = Path(output_dir)
        self.depth = depth
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def move_to_label(self, board, move):
        if not move or move == chess.Move.null(): return 0
        from_sq = move.from_square
        to_sq = move.to_square
        piece = board.piece_at(from_sq)
        if piece is None: return 0
        fen_sq = to_sq ^ 56
        piece_map = {chess.PAWN: 0, chess.KNIGHT: 1, chess.BISHOP: 2, chess.ROOK: 3, chess.QUEEN: 4, chess.KING: 5}
        p_idx = piece_map[piece.piece_type]
        if piece.color == chess.BLACK: p_idx += 6
        return min(fen_sq * 12 + p_idx, 767)

    def count_material(self, board):
        vals = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3, chess.ROOK: 5, chess.QUEEN: 9, chess.KING: 0}
        total = 0
        for pt, val in vals.items():
            total += len(board.pieces(pt, chess.WHITE)) * val
            total -= len(board.pieces(pt, chess.BLACK)) * val
        return total

    def calculate_rs(self, board, eval_score):
        piece_count = len(board.piece_map())
        if piece_count < 10: return 1.0
        if piece_count < 16: return 0.7
        if not board.is_check() and abs(eval_score) < 30:
            return min(0.8, 0.4 + (30 - abs(eval_score)) / 100.0)
        return 0.1

    def get_top_moves_safe(self, board, engine, depth):
        try:
            res = engine.analyse(board, chess.engine.Limit(depth=depth), multipv=3)
            if not isinstance(res, list): res = [res]
            moves = [r["pv"][0].uci() for r in res if "pv" in r]
            labels = [self.move_to_label(board, r["pv"][0]) for r in res if "pv" in r]
            return moves, labels
        except: return [], []

    def analyze_full(self, board, engine):
        try:
            res8 = engine.analyse(board, chess.engine.Limit(depth=8))
            res16_multi = engine.analyse(board, chess.engine.Limit(depth=16), multipv=3)
            res26 = engine.analyse(board, chess.engine.Limit(depth=26))

            if not isinstance(res16_multi, list): res16_multi = [res16_multi]

            m8 = res8.get("pv", [None])[0]
            m16 = res16_multi[0].get("pv", [None])[0]
            m26 = res26.get("pv", [None])[0]

            cp = res16_multi[0]["score"].relative.score(mate_score=10000) or 0
            # Improved HARENN labels (continuous, calibrated with harenn_ctrl thresholds)
            # RHO: eval volatility + move instability + near-zero uncertainty
            cp26 = res26["score"].relative.score(mate_score=10000) or 0
            swing = max(abs(cp - cp26), abs((res16_multi[0]["score"].relative.score(mate_score=10000) or 0) - cp26))
            rho_vol = min(swing / 250.0, 0.6)
            rho = rho_vol
            if m8 != m16:
                rho += 0.15
            if m16 != m26:
                rho += 0.30
            if abs(cp) < 30:
                rho += 0.25
            elif abs(cp) < 80:
                rho += 0.10
            if board.is_check():
                rho += 0.15
            rho = min(rho, 1.0)

            # TAU: best-move instability + top3 overlap (if available)
            tau = 0.0
            if m8 != m16:
                tau += 0.35
            if m16 != m26:
                tau += 0.45
            # top3 overlap between 16 and 26 if multipv available
            if len(res16_multi) >= 3:
                # res16_multi has 3 PVs at depth 16, but we need top3 at depth 26 as well
                # Use res16_multi top3 vs m26 single -> approximate
                m16_top3 = [r["pv"][0].uci() for r in res16_multi[:3] if "pv" in r and r["pv"]]
                # For m26 we only have single PV, so overlap at least 1 if m16==m26
                overlap = 1 if m16 in m16_top3 and m16 == m26 else 0
                if overlap == 0 and len(m16_top3) >= 2:
                    tau += 0.15
            else:
                # Fallback: score volatility among multipv
                if len(res16_multi) > 1:
                    diffs = [abs(cp - (r["score"].relative.score(mate_score=10000) or 0)) for r in res16_multi[1:]]
                    tau = max(tau, min((sum(diffs) / len(diffs)) / 120.0, 0.5))
            tau = min(tau, 1.0)

            m16, l16 = [r["pv"][0].uci() for r in res16_multi], [self.move_to_label(board, r["pv"][0]) for r in res16_multi]
            m20, l20 = self.get_top_moves_safe(board, engine, 20)
            m24, l24 = self.get_top_moves_safe(board, engine, 24)

            return {
                "cp": cp, "tau": round(tau, 4), "rho": round(min(rho, 1.0), 4), 
                "rs": self.calculate_rs(board, cp),
                "m16": m16, "l16": l16, "m20": m20, "l20": l20, "m24": m24, "l24": l24
            }
        except: return None

    def worker_task(self, game_id):
        p_name = multiprocessing.current_process().name
        worker_id = p_name.split('-')[-1] if '-' in p_name else "0"
        engine = chess.engine.SimpleEngine.popen_uci(self.engine_path)
        positions, batch_num, board = [], 0, chess.Board()
        total_game_pos = 0
        try:
            for move_num in range(1, 100):
                if board.is_game_over(): break
                if random.random() < 0.08:
                    move = random.choice(list(board.legal_moves))
                else:
                    move = engine.play(board, chess.engine.Limit(time=0.04)).move
                board.push(move)
                
                # BỎ LỌC, LẤY TẤT CẢ SAU NƯỚC 6
                if move_num > 6:
                    data = self.analyze_full(board, engine)
                    if data:
                        total_game_pos += 1
                        print(f"  [W{worker_id} | G{game_id}] Pos {total_game_pos}: Eval {data['cp']}", flush=True)
                        pos = TrainingPosition(
                            fen=board.fen(), stm=0 if board.turn == chess.WHITE else 1,
                            eval_score=data["cp"], depth=16,
                            best_move=data["m16"][0] if data["m16"] else "0000",
                            best_move_label=self.move_to_label(board, chess.Move.from_uci(data["m16"][0])) if data["m16"] else 0,
                            best_moves_d16=data["m16"], best_moves_d20=data["m20"], best_moves_d24=data["m24"],
                            best_move_labels_d16=data["l16"], best_move_labels_d20=data["l20"], best_move_labels_d24=data["l24"],
                            game_result=1, material=self.count_material(board), piece_count=len(board.piece_map()),
                            tau=data["tau"], rho=data["rho"], rs=data["rs"]
                        )
                        positions.append(asdict(pos))
                        if len(positions) >= 5:
                            ts = time.strftime("%H%M%S")
                            with open(self.output_dir / f"hnn_g{game_id}_b{batch_num}_{ts}.json", 'w') as f:
                                json.dump({"positions": positions}, f, indent=2)
                            positions, batch_num = [], batch_num + 1
            return total_game_pos
        finally: engine.quit()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", "-e", default="../nextfish.exe")
    parser.add_argument("--output", "-o", default="D:/nextfish/data")
    parser.add_argument("--games", "-g", type=int, default=100)
    parser.add_argument("--concurrency", "-j", type=int, default=multiprocessing.cpu_count() - 1)
    args = parser.parse_args()
    print(f"=== HARENN Turbo Generator V8 (Raw Data) ===")
    gen = ParallelGenerator(args.engine, args.output)
    total_pos, start_time = 0, time.time()
    with ProcessPoolExecutor(max_workers=args.concurrency) as executor:
        futures = {executor.submit(gen.worker_task, i): i for i in range(args.games)}
        for future in as_completed(futures):
            res = future.result()
            total_pos += res
            elapsed = time.time() - start_time
            print(f"[{time.strftime('%H:%M:%S')}] Total Progress: {total_pos} Pos | Speed: {total_pos/elapsed:.2f} pos/s", flush=True)

if __name__ == "__main__": main()
