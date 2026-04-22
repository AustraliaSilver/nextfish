#!/usr/bin/env python3
"""
Simplified HARENN Training Data Generator

Usage:
    python simple_generate.py --engine ./nextfish.exe --games 100
"""

import argparse
import os
import sys
import json
import time
import random
import logging
from pathlib import Path
from dataclasses import dataclass
import chess
import chess.engine

logging.getLogger("chess.engine").setLevel(logging.CRITICAL)


@dataclass
class TrainingPosition:
    fen: str
    stm: int
    eval_score: int
    depth: int
    best_move: str
    best_move_label: int  # Classification label for best move (0-767)
    # Top 3 best moves at different depths
    best_moves_d16: list  # Top 3 moves at depth 16
    best_moves_d20: list  # Top 3 moves at depth 20
    best_moves_d24: list  # Top 3 moves at depth 24
    best_move_labels_d16: list  # Labels for depth 16 moves
    best_move_labels_d20: list  # Labels for depth 20 moves
    best_move_labels_d24: list  # Labels for depth 24 moves
    game_result: int
    material: int
    piece_count: int
    tau: float
    rho: float
    rs: float


class SimpleDataGenerator:
    def __init__(self, engine_path: str, output_dir: str):
        self.engine_path = engine_path
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.engine = None
        self.engine_disabled = False
        self.suppress_engine_errors = False

    def get_engine(self):
        """Get or create engine instance"""
        if self.engine_disabled:
            return None
        if self.engine is None:
            self.engine = self.create_engine("play")
        return self.engine

    def create_engine(self, role: str = "engine", retries: int = 3, delay: float = 0.5):
        """Create engine instance with retry to survive transient subprocess failures."""
        last_error = None
        for attempt in range(1, retries + 1):
            try:
                return chess.engine.SimpleEngine.popen_uci(self.engine_path)
            except Exception as error:
                last_error = error
                if not self.suppress_engine_errors:
                    print(
                        f"Engine start failed ({role}, attempt {attempt}/{retries}): {error}"
                    )
                if attempt < retries:
                    time.sleep(delay)
        raise RuntimeError(f"Cannot start {role} engine: {last_error}")

    def close_engine(self):
        """Close engine"""
        if self.engine:
            self.engine.quit()
            self.engine = None

    def move_to_label(self, board: chess.Board, move: chess.Move) -> int:
        """Convert chess move to classification label (0-767)"""
        # 768 = 64 squares * 12 piece types
        # Square mapping: A8=0, ..., H1=63 (FEN style)
        # Piece mapping: W_PAWN..W_KING=0..5, B_PAWN..B_KING=6..11
        
        from_sq = move.from_square
        to_sq = move.to_square
        piece = board.piece_at(from_sq)
        
        if piece is None:
            return 0  # Invalid move
        
        # Map Stockfish square (A1=0) to FEN-style square (A8=0)
        fen_sq = to_sq ^ 56
        
        # Map piece type to 0-11
        piece_map = {
            chess.PAWN: 0,
            chess.KNIGHT: 1,
            chess.BISHOP: 2,
            chess.ROOK: 3,
            chess.QUEEN: 4,
            chess.KING: 5,
        }
        
        piece_idx = piece_map[piece.piece_type]
        if piece.color == chess.BLACK:
            piece_idx += 6
        
        label = fen_sq * 12 + piece_idx
        return min(label, 767)

    def count_material(self, board: chess.Board) -> int:
        """Count material balance"""
        piece_values = {
            chess.PAWN: 1,
            chess.KNIGHT: 3,
            chess.BISHOP: 3,
            chess.ROOK: 5,
            chess.QUEEN: 9,
            chess.KING: 0,
        }
        total = 0
        for piece_type in piece_values:
            total += (
                len(board.pieces(piece_type, chess.WHITE)) * piece_values[piece_type]
            )
            total -= (
                len(board.pieces(piece_type, chess.BLACK)) * piece_values[piece_type]
            )
        return total

    def calculate_tau_engine(self, board: chess.Board, depth: int = 12) -> float:
        """Calculate tactical complexity (τ) using engine evaluation volatility"""
        try:
            # Create separate engine to avoid state conflicts
            tau_engine = chess.engine.SimpleEngine.popen_uci(self.engine_path)
            
            try:
                # Analyze current position
                result = tau_engine.analyse(board, chess.engine.Limit(depth=depth))
                current_score = result.get("score")
                if current_score:
                    current_cp = current_score.relative.score(mate_score=10000)
                else:
                    current_cp = 0

                # Analyze top 3 moves to measure volatility
                scores = []
                if result.get("pv"):
                    for i, move in enumerate(result["pv"][:3]):
                        board_copy = board.copy()
                        try:
                            board_copy.push(move)
                            move_result = tau_engine.analyse(board_copy, chess.engine.Limit(depth=depth-4))
                            move_score = move_result.get("score")
                            if move_score:
                                move_cp = move_score.relative.score(mate_score=10000)
                                scores.append(abs(move_cp - current_cp))
                        except:
                            pass

                # Calculate volatility as standard deviation of score differences
                if scores:
                    volatility = (sum(scores) / len(scores)) / 100.0  # Normalize to centipawns
                    return min(volatility, 1.0)
                else:
                    return 0.0
            finally:
                tau_engine.quit()
        except:
            return 0.0

    def calculate_rho(self, board: chess.Board, eval_score: int, depth: int = 16) -> float:
        """Calculate risk measure (ρ) based on evaluation variance and tactical factors"""
        try:
            # Create separate engine to avoid state conflicts
            rho_engine = chess.engine.SimpleEngine.popen_uci(self.engine_path)
            
            try:
                risk = 0.0

                # 1. Best move consistency check (CRITICAL: depth 8 vs 16 vs 24)
                shallow_result = rho_engine.analyse(board, chess.engine.Limit(depth=8))
                medium_result = rho_engine.analyse(board, chess.engine.Limit(depth=depth))
                deep_result = rho_engine.analyse(board, chess.engine.Limit(depth=24))
                
                shallow_best = shallow_result.get("pv", [chess.Move.null()])[0] if shallow_result.get("pv") else chess.Move.null()
                medium_best = medium_result.get("pv", [chess.Move.null()])[0] if medium_result.get("pv") else chess.Move.null()
                deep_best = deep_result.get("pv", [chess.Move.null()])[0] if deep_result.get("pv") else chess.Move.null()
                
                # If best move changes between any depths, Rho = 1.0 (high risk)
                if shallow_best != medium_best or medium_best != deep_best:
                    return 1.0

                # 2. Evaluation variance (how unstable is the position?)
                if shallow_result.get("score") and medium_result.get("score") and deep_result.get("score"):
                    shallow_cp = shallow_result["score"].relative.score(mate_score=10000)
                    medium_cp = medium_result["score"].relative.score(mate_score=10000)
                    deep_cp = deep_result["score"].relative.score(mate_score=10000)
                    
                    eval_diff_1 = abs(shallow_cp - medium_cp)
                    eval_diff_2 = abs(medium_cp - deep_cp)
                    eval_diff_3 = abs(shallow_cp - deep_cp)
                    
                    risk += min(eval_diff_3 / 200.0, 0.4)  # Higher difference = more risk

                # 3. Evaluation balance
                if abs(eval_score) < 50:
                    risk += 0.3  # Balanced positions are more risky
                elif abs(eval_score) < 100:
                    risk += 0.2

                # 4. Tactical complexity
                if board.is_check():
                    risk += 0.25
                if board.is_attacked_by(board.king(board.turn)):
                    risk += 0.15

                # 5. Material imbalance risk
                piece_count = len(board.piece_map())
                if piece_count < 16:
                    risk += 0.2  # Fewer pieces = more decisive

                # 6. Hanging pieces
                for sq, piece in board.piece_map().items():
                    if board.is_attacked_by(not board.turn, sq) and not board.is_attacked_by(board.turn, sq):
                        risk += 0.1

                return min(risk, 1.0)
            finally:
                rho_engine.quit()
        except Exception as e:
            print(f"Error in calculate_rho: {e}")
            return 0.5

    def calculate_rs(self, board: chess.Board, eval_score: int) -> float:
        """Calculate endgame/quiet factor (rs) based on game phase"""
        try:
            piece_count = len(board.piece_map())
            material = self.count_material(board)

            # 1. Endgame detection
            if piece_count < 10:
                return 1.0  # Clear endgame
            elif piece_count < 16:
                return 0.7  # Late middlegame/early endgame
            elif piece_count < 24:
                return 0.4  # Middlegame
            else:
                return 0.1  # Opening/early middlegame

            # 2. Quiet position detection (no tactical threats)
            if not board.is_check() and abs(eval_score) < 30:
                return min(0.8, 0.4 + (30 - abs(eval_score)) / 100.0)

            return 0.4
        except:
            return 0.5

    def analyze_position(self, fen: str, depth: int = 16, engine=None) -> dict:
        def pick_best_move(
            board: chess.Board,
            analysis_engine,
            time_limit: float = 0.03,
            allowed_moves=None,
        ):
            try:
                result = analysis_engine.play(
                    board, chess.engine.Limit(time=time_limit), root_moves=allowed_moves
                )
                move = result.move
                if move is not None and move in board.legal_moves:
                    return move
            except Exception:
                return None
            return None

        def get_top_moves(board, analysis_engine):
            if analysis_engine is None:
                fallback = list(board.legal_moves)[:3]
                return [move.uci() for move in fallback], [
                    self.move_to_label(board, move) for move in fallback
                ]
            moves = []
            labels = []
            remaining_moves = list(board.legal_moves)
            for _ in range(3):
                if not remaining_moves:
                    break
                move = pick_best_move(
                    board, analysis_engine, time_limit=0.03, allowed_moves=remaining_moves
                )
                if move is None:
                    break
                moves.append(move.uci())
                labels.append(self.move_to_label(board, move))
                remaining_moves = [candidate for candidate in remaining_moves if candidate != move]
            return moves, labels
        
        try:
            # Use the provided engine or get a new one
            if engine is not None:
                analysis_engine = engine
            else:
                if self.engine_disabled:
                    analysis_engine = None
                else:
                    try:
                        analysis_engine = self.get_engine()
                    except Exception:
                        analysis_engine = None
                        self.engine_disabled = True
                        self.suppress_engine_errors = True

            board = chess.Board(fen)
            
            cp = self.count_material(board) * 100
            best_move = (
                pick_best_move(board, analysis_engine, time_limit=0.05)
                if analysis_engine is not None
                else None
            )
            if best_move is None:
                legal_moves = list(board.legal_moves)
                best_move = legal_moves[0] if legal_moves else None

            material = self.count_material(board)
            piece_count = len(board.piece_map())
            
            # Lightweight stability metrics that avoid heavy analyse() calls.
            legal_count = len(list(board.legal_moves))
            tau = min(legal_count / 50.0, 1.0)
            rho = 1.0 if board.is_check() else (0.6 if abs(cp) < 80 else 0.3)
            rs = self.calculate_rs(board, cp)
            
            best_move_label = 0
            if best_move and best_move != chess.Move.null():
                try:
                    best_move_label = self.move_to_label(board, best_move)
                except:
                    pass
            
            # Get top moves without analyse() to avoid protocol desync errors.
            best_moves_d16, best_move_labels_d16 = get_top_moves(board, analysis_engine)
            best_moves_d20, best_move_labels_d20 = get_top_moves(board, analysis_engine)
            best_moves_d24, best_move_labels_d24 = get_top_moves(board, analysis_engine)

            # Don't close engine if it was passed in
            if engine is None:
                try:
                    self.close_engine()
                except:
                    pass

            return {
                "score": cp or 0,
                "best_move": best_move.uci() if best_move else "0000",
                "best_move_label": best_move_label,
                "depth": depth,
                "material": material,
                "piece_count": piece_count,
                "tau": tau,
                "rho": rho,
                "rs": rs,
                "best_moves_d16": best_moves_d16,
                "best_moves_d20": best_moves_d20,
                "best_moves_d24": best_moves_d24,
                "best_move_labels_d16": best_move_labels_d16,
                "best_move_labels_d20": best_move_labels_d20,
                "best_move_labels_d24": best_move_labels_d24,
                "analysis_error": False,
            }
        except Exception as e:
            print(f"Error analyzing: {e}")
            return {
                "score": 0,
                "best_move": "0000",
                "best_move_label": 0,
                "depth": depth,
                "material": 0,
                "piece_count": 0,
                "tau": 0.0,
                "rho": 0.5,
                "rs": 0.5,
                "best_moves_d16": [],
                "best_moves_d20": [],
                "best_moves_d24": [],
                "best_move_labels_d16": [],
                "best_move_labels_d20": [],
                "best_move_labels_d24": [],
                "analysis_error": True,
            }

    def analyze_position_real(self, fen: str, depth: int = 16) -> dict:
        """Real engine analysis for score, tau/rho, and depth-specific top moves."""
        analysis_engine = None
        board = chess.Board(fen)
        material = self.count_material(board)
        piece_count = len(board.piece_map())

        def _extract_top3(info):
            moves, labels = [], []
            info_lines = info if isinstance(info, list) else [info]
            for line in info_lines:
                pv = line.get("pv", [])
                if not pv:
                    continue
                move = pv[0]
                if move is None or move == chess.Move.null():
                    continue
                if move not in board.legal_moves:
                    continue
                uci = move.uci()
                if uci in moves:
                    continue
                moves.append(uci)
                labels.append(self.move_to_label(board, move))
                if len(moves) == 3:
                    break
            return moves, labels

        def _score_cp(info):
            info_lines = info if isinstance(info, list) else [info]
            for line in info_lines:
                score = line.get("score")
                if score:
                    return score.relative.score(mate_score=10000) or 0
            return 0

        try:
            analysis_engine = self.create_engine("analysis", retries=2, delay=0.2)

            info_d16 = analysis_engine.analyse(
                board, chess.engine.Limit(depth=16), multipv=3
            )
            info_d20 = analysis_engine.analyse(
                board, chess.engine.Limit(depth=20), multipv=3
            )
            info_d24 = analysis_engine.analyse(
                board, chess.engine.Limit(depth=24), multipv=3
            )

            best_moves_d16, best_move_labels_d16 = _extract_top3(info_d16)
            best_moves_d20, best_move_labels_d20 = _extract_top3(info_d20)
            best_moves_d24, best_move_labels_d24 = _extract_top3(info_d24)

            cp = _score_cp(info_d16)
            best_move = best_moves_d16[0] if best_moves_d16 else "0000"
            best_move_label = best_move_labels_d16[0] if best_move_labels_d16 else 0

            # Real tau from score volatility across PV continuations.
            tau_scores = []
            if isinstance(info_d16, list):
                for line in info_d16[:3]:
                    score = line.get("score")
                    if score:
                        tau_scores.append(score.relative.score(mate_score=10000) or 0)
            tau = 0.0
            if len(tau_scores) >= 2:
                diffs = [abs(value - cp) for value in tau_scores]
                tau = min((sum(diffs) / len(diffs)) / 100.0, 1.0)

            # Real rho from best-move stability + eval swing across depths.
            d16_best = best_moves_d16[0] if best_moves_d16 else None
            d20_best = best_moves_d20[0] if best_moves_d20 else None
            d24_best = best_moves_d24[0] if best_moves_d24 else None
            rho = 0.0
            if not (d16_best and d20_best and d24_best):
                rho += 0.4
            elif d16_best != d20_best or d20_best != d24_best:
                rho += 0.6
            cp_d20 = _score_cp(info_d20)
            cp_d24 = _score_cp(info_d24)
            rho += min(abs(cp - cp_d24) / 250.0, 0.35)
            rho += min(abs(cp_d20 - cp_d24) / 250.0, 0.25)
            rho = min(rho, 1.0)

            rs = self.calculate_rs(board, cp)

            return {
                "score": cp,
                "best_move": best_move,
                "best_move_label": best_move_label,
                "depth": depth,
                "material": material,
                "piece_count": piece_count,
                "tau": tau,
                "rho": rho,
                "rs": rs,
                "best_moves_d16": best_moves_d16,
                "best_moves_d20": best_moves_d20,
                "best_moves_d24": best_moves_d24,
                "best_move_labels_d16": best_move_labels_d16,
                "best_move_labels_d20": best_move_labels_d20,
                "best_move_labels_d24": best_move_labels_d24,
                "analysis_error": False,
            }
        except Exception:
            return {
                "score": 0,
                "best_move": "0000",
                "best_move_label": 0,
                "depth": depth,
                "material": material,
                "piece_count": piece_count,
                "tau": 0.0,
                "rho": 0.5,
                "rs": 0.5,
                "best_moves_d16": [],
                "best_moves_d20": [],
                "best_moves_d24": [],
                "best_move_labels_d16": [],
                "best_move_labels_d20": [],
                "best_move_labels_d24": [],
                "analysis_error": True,
            }
        finally:
            if analysis_engine is not None:
                try:
                    analysis_engine.quit()
                except:
                    pass

    def is_quality_position(
        self, board: chess.Board, eval_score: int, tau: float = 0.0
    ) -> bool:
        """Check if position meets quality criteria - FAST checks only"""

        # 1. Not in endgame (need at least 10 pieces total)
        if len(board.piece_map()) < 10:
            return False

        # 2. Should have both kings
        if not board.king(chess.WHITE) or not board.king(chess.BLACK):
            return False

        # 3. Should have pawns remaining
        pawns = len(board.pieces(chess.PAWN, chess.WHITE)) + len(
            board.pieces(chess.PAWN, chess.BLACK)
        )
        if pawns < 2:
            return False

        return True

    def generate_games(self, num_games: int, batch_size: int = 500) -> list | None:
        """Generate self-play games and collect positions"""

        positions = []
        batch_num = 0

        print(f"Generating {num_games} games...")
        print(f"Saving every {batch_size} positions")

        # Use engine for play; analysis uses isolated per-call engine to keep state clean.
        play_engine = None
        try:
            play_engine = self.get_engine()
        except Exception as e:
            print(f"Engine unavailable, switching to fallback mode: {e}")
            self.engine_disabled = True
            self.suppress_engine_errors = True

        # Restart engines every 50 games to prevent memory issues
        games_since_restart = 0

        for game_idx in range(num_games):
            try:
                # Restart engines periodically
                if games_since_restart >= 50:
                    if play_engine is not None:
                        self.close_engine()
                        play_engine = self.get_engine()
                    games_since_restart = 0

                board = chess.Board()

                # Play game
                for move_num in range(100):
                    # Use engine to pick move with some randomness for diversity
                    if random.random() < 0.1:  # 10% random moves for diversity
                        move = random.choice(list(board.legal_moves))
                    else:
                        if play_engine is None:
                            legal_moves = list(board.legal_moves)
                            move = random.choice(legal_moves) if legal_moves else None
                        else:
                        # Use play with short time limit to auto-set position and get move
                            try:
                                result = play_engine.play(
                                    board, chess.engine.Limit(time=0.05)
                                )
                                move = result.move
                            except Exception as e:
                                print(f"Engine play failed: {e} - restarting play engine")
                                try:
                                    self.close_engine()
                                except:
                                    pass
                                try:
                                    play_engine = self.get_engine()
                                except Exception:
                                    play_engine = None
                                legal_moves = list(board.legal_moves)
                                move = random.choice(legal_moves) if legal_moves else None

                    if move is None:
                        break

                    # Guard against illegal moves from a desynced/unstable engine.
                    # `Board.push()` does not fully validate legality, so we validate here.
                    if move not in board.legal_moves:
                        print(
                            f"Warning: illegal move from engine: {move.uci()} in {board.fen()} - resyncing engine"
                        )
                        if not self.engine_disabled:
                            try:
                                self.close_engine()
                            except:
                                pass
                            try:
                                play_engine = self.get_engine()
                            except Exception:
                                play_engine = None
                                self.engine_disabled = True
                                self.suppress_engine_errors = True
                        legal_moves = list(board.legal_moves)
                        if not legal_moves:
                            break
                        move = random.choice(legal_moves)

                    board.push(move)

                    # Quality-controlled sampling - analyze all positions after move 6
                    if move_num >= 6:
                        analysis = self.analyze_position_real(board.fen(), depth=16)
                        if analysis.get("analysis_error"):
                            continue
                        print(f"  Analyzed position {len(positions)+1} (game {game_idx+1}, move {move_num})")

                        # Apply quality filters (includes τ check)
                        if self.is_quality_position(
                            board, analysis["score"], analysis["tau"]
                        ):
                            pos = TrainingPosition(
                                fen=board.fen(),
                                stm=0 if board.turn == chess.WHITE else 1,
                                eval_score=analysis["score"],
                                depth=analysis["depth"],
                                best_move=analysis["best_move"],
                                best_move_label=analysis["best_move_label"],
                                best_moves_d16=analysis["best_moves_d16"],
                                best_moves_d20=analysis["best_moves_d20"],
                                best_moves_d24=analysis["best_moves_d24"],
                                best_move_labels_d16=analysis["best_move_labels_d16"],
                                best_move_labels_d20=analysis["best_move_labels_d20"],
                                best_move_labels_d24=analysis["best_move_labels_d24"],
                                game_result=1,
                                material=analysis["material"],
                                piece_count=analysis["piece_count"],
                                tau=analysis["tau"],
                                rho=analysis["rho"],
                                rs=analysis["rs"],
                            )
                            positions.append(pos)

                            # Save batch when reaching limit
                            if len(positions) >= batch_size:
                                timestamp = time.strftime("%Y%m%d_%H%M%S")
                                filename = f"harenn_batch_{batch_num}_{timestamp}.json"
                                self.save_positions(positions, filename)
                                positions = []  # Clear memory
                                batch_num += 1

                    # Check game over
                    if board.is_game_over():
                        break

                games_since_restart += 1

                # Determine game result
                if board.is_checkmate():
                    result = board.result()
                    game_result = {"1-0": 2, "0-1": 0, "1/2-1/2": 1}.get(result, 1)
                elif board.is_game_over():
                    game_result = 1
                else:
                    game_result = 1

                # Update game result for all positions from this game
                for pos in positions[-30:]:
                    pos.game_result = game_result

            except Exception as e:
                print(f"Error in game {game_idx}: {e}")

            if (game_idx + 1) % 10 == 0:
                print(
                    f"  Games: {game_idx + 1}/{num_games}, Positions: {len(positions)}"
                )

        # Close engines
        if play_engine is not None:
            self.close_engine()

        return positions

    def save_positions(self, positions: list, filename: str):
        """Save positions to JSON file"""

        output_path = self.output_dir / filename

        # Calculate statistics
        eval_scores = [p.eval_score for p in positions]
        tau_values = [p.tau for p in positions]
        rho_values = [p.rho for p in positions]
        rs_values = [p.rs for p in positions]

        data = {
            "positions": [
                {
                    "fen": p.fen,
                    "stm": p.stm,
                    "eval_score": p.eval_score,
                    "depth": p.depth,
                    "best_move": p.best_move,
                    "best_move_label": p.best_move_label,
                    "best_moves_d16": p.best_moves_d16,
                    "best_moves_d20": p.best_moves_d20,
                    "best_moves_d24": p.best_moves_d24,
                    "best_move_labels_d16": p.best_move_labels_d16,
                    "best_move_labels_d20": p.best_move_labels_d20,
                    "best_move_labels_d24": p.best_move_labels_d24,
                    "game_result": p.game_result,
                    "material": p.material,
                    "piece_count": p.piece_count,
                    "tau": round(p.tau, 4),
                    "rho": round(p.rho, 4),
                    "rs": round(p.rs, 4),
                }
                for p in positions
            ],
            "stats": {
                "total_positions": len(positions),
                "avg_eval": round(sum(eval_scores) / len(eval_scores), 2)
                if eval_scores
                else 0,
                "avg_tau": round(sum(tau_values) / len(tau_values), 4)
                if tau_values
                else 0,
                "avg_rho": round(sum(rho_values) / len(rho_values), 4)
                if rho_values
                else 0,
                "avg_rs": round(sum(rs_values) / len(rs_values), 4)
                if rs_values
                else 0,
                "white_wins": sum(1 for p in positions if p.game_result == 2),
                "black_wins": sum(1 for p in positions if p.game_result == 0),
                "draws": sum(1 for p in positions if p.game_result == 1),
            },
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        }

        try:
            with open(output_path, "w") as f:
                json.dump(data, f, indent=2)
        except PermissionError:
            fallback_dir = Path("./data")
            fallback_dir.mkdir(parents=True, exist_ok=True)
            output_path = fallback_dir / filename
            with open(output_path, "w") as f:
                json.dump(data, f, indent=2)

        print(f"Saved {len(positions)} positions to {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Simple HARENN Data Generator")
    parser.add_argument(
        "--engine", "-e", default="./nextfish.exe", help="Path to chess engine"
    )
    parser.add_argument("--output", "-o", default="./data", help="Output directory")
    parser.add_argument("--games", "-g", type=int, default=100, help="Number of games")
    parser.add_argument(
        "--continuous",
        "-c",
        action="store_true",
        help="Run continuously until Ctrl+C (ignores --games)",
    )
    parser.add_argument(
        "--batch",
        "-b",
        type=int,
        default=500,
        help="Save batch every N positions",
    )

    args = parser.parse_args()

    print(f"Engine: {args.engine}")
    print(f"Output: {args.output}")
    print(f"Batch size: {args.batch}")

    generator = SimpleDataGenerator(args.engine, args.output)

    if args.continuous:
        print("Running CONTINUOUSLY - press Ctrl+C to stop")
        total = 0
        while True:
            try:
                positions = generator.generate_games(1000, batch_size=args.batch)
                if positions is None:
                    print("Stopped due to engine initialization failure")
                    break
                total += len(positions)
                print(f"  Total so far: {total} positions")
            except KeyboardInterrupt:
                print(f"\nStopped! Total: {total} positions")
                break
    else:
        print(f"Games: {args.games}")
        positions = generator.generate_games(args.games, batch_size=args.batch)
        if positions is None:
            print("\nDone with engine initialization error (no positions saved)")
            return

        timestamp = time.strftime("%Y%m%d_%H%M%S")
        filename = f"harenn_positions_{timestamp}.json"

        generator.save_positions(positions, filename)

        print(f"\nDone! Generated {len(positions)} positions")


if __name__ == "__main__":
    main()
