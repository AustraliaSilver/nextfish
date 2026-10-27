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
import uuid

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

    def __init__(self, engine_path, output_dir, depth=16, time_budget=None, worker_id=None):
        self.worker_id = worker_id
        self.time_budget = time_budget
        self.start_time = time.time()

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

            res20 = engine.analyse(board, chess.engine.Limit(depth=20))



            if not isinstance(res16_multi, list): res16_multi = [res16_multi]



            m8 = res8.get("pv", [None])[0]

            m16 = res16_multi[0].get("pv", [None])[0]

            m20 = res20.get("pv", [None])[0]



            cp = res16_multi[0]["score"].relative.score(mate_score=10000) or 0

            # Search instability labels: measure how much the engine's opinion
            # changes when the search depth increases. A position whose eval
            # swings or whose best move changes between depths is unresolved
            # and benefits from more search.
            cp20 = res20["score"].relative.score(mate_score=10000) or 0
            cp8 = res8["score"].relative.score(mate_score=10000) or 0

            # Eval swing between depths (absolute, in centipawns)
            swing_8_16 = abs(cp8 - cp)
            swing_16_20 = abs(cp - cp20)
            max_swing = max(swing_8_16, swing_16_20)

            # RHO (horizon risk): how much the eval moves between depths.
            # Normalized so that 200 cp of swing maps to 1.0.
            rho = min(max_swing / 200.0, 1.0)

            # TAU (tactical complexity): how often the best move changes
            # between depths. With 3 depths there are 2 transitions.
            transitions = 0
            changes = 0
            if m8 is not None and m16 is not None:
                transitions += 1
                if m8 != m16:
                    changes += 1
            if m16 is not None and m20 is not None:
                transitions += 1
                if m16 != m20:
                    changes += 1
            if transitions > 0:
                tau = changes / transitions
            else:
                tau = 0.0

            # Bonus for near-zero eval (positions where the outcome is unclear)
            if abs(cp) < 30:
                rho = min(rho + 0.15, 1.0)
                tau = min(tau + 0.15, 1.0)
            elif abs(cp) < 80:
                rho = min(rho + 0.05, 1.0)

            # Bonus for check (tactical positions)
            if board.is_check():
                rho = min(rho + 0.10, 1.0)
                tau = min(tau + 0.10, 1.0)

            # RS (Confidence Score / Resolution): Measures how confident the
            # engine is in its evaluation and move choice compared to potential error/deviation.
            # 1. Depth Stability: Eval deviation across depth 8, 16, and 20
            max_eval_dev = max(abs(cp8 - cp), abs(cp - cp20), abs(cp8 - cp20))
            s_stability = max(0.0, 1.0 - (max_eval_dev / 150.0))

            # 2. Move Consistency: Agreement of best move across depths
            if m8 is not None and m16 is not None and m20 is not None:
                if m8 == m16 == m20:
                    s_agreement = 1.0
                elif m16 == m20:
                    s_agreement = 0.75
                elif m8 == m20:
                    s_agreement = 0.50
                else:
                    s_agreement = 0.25
            else:
                s_agreement = 0.50

            # 3. Decision Margin: Dominance of best move over second best move in MultiPV
            if len(res16_multi) >= 2 and 'score' in res16_multi[1] and res16_multi[1].get('pv'):
                cp_second = res16_multi[1]['score'].relative.score(mate_score=10000) or 0
                margin = max(0, cp - cp_second)
                s_margin = min(1.0, margin / 80.0)
            else:
                s_margin = 0.50

            # Composite confidence score
            rs = 0.45 * s_stability + 0.35 * s_agreement + 0.20 * s_margin
            rs = max(0.05, min(0.99, rs))
            m16, l16 = [r["pv"][0].uci() for r in res16_multi], [self.move_to_label(board, r["pv"][0]) for r in res16_multi]

            m20, l20 = self.get_top_moves_safe(board, engine, 20)

            m24, l24 = self.get_top_moves_safe(board, engine, 20)



            return {

                "cp": cp, "tau": round(tau, 4), "rho": round(min(rho, 1.0), 4), 

                "rs": round(rs, 4),
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

            if self.time_budget and (time.time() - self.start_time) > self.time_budget:
                print(f"  [W{worker_id} | G{game_id}] Time budget exceeded, stopping", flush=True)
                return total_game_pos
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

                            worker_tag = f"w{self.worker_id}_" if self.worker_id else ""
                            rand_id = uuid.uuid4().hex[:6]
                            with open(self.output_dir / f"hnn_{worker_tag}g{game_id}_b{batch_num}_{ts}_{rand_id}.json", 'w') as f:

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
    parser.add_argument("--time-budget", type=int, default=None, help="Stop after N seconds (leave time for commit)")
    parser.add_argument("--worker-id", "-w", default=None, help="Worker identifier for naming")

    args = parser.parse_args()

    print(f"=== HARENN Turbo Generator V8 (Raw Data) ===")

    gen = ParallelGenerator(args.engine, args.output, time_budget=args.time_budget, worker_id=args.worker_id)

    total_pos, start_time = 0, time.time()

    with ProcessPoolExecutor(max_workers=args.concurrency) as executor:

        futures = {executor.submit(gen.worker_task, i): i for i in range(args.games)}

        for future in as_completed(futures):

            res = future.result()

            total_pos += res

            elapsed = time.time() - start_time

            print(f"[{time.strftime('%H:%M:%S')}] Total Progress: {total_pos} Pos | Speed: {total_pos/elapsed:.2f} pos/s", flush=True)



if __name__ == "__main__": main()

