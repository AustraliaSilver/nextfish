import chess
import json
import numpy as np

class PositionAnalyzer:
    @staticmethod
    def calculate_tau(board: chess.Board) -> float:
        """
        Tính toán độ phức tạp chiến thuật (Tau) chuyên sâu.
        Dựa trên: Khả năng di chuyển, mật độ ăn quân, áp lực lên Vua và các quân bị treo.
        """
        us = board.turn
        them = not us
        
        # 1. Mobility (Tỉ lệ nước đi hợp lệ)
        legal_moves = list(board.legal_moves)
        mobility_score = len(legal_moves) / 60.0
        
        # 2. Capture Intensity (Mật độ các quân có thể ăn nhau)
        captures = [m for m in legal_moves if board.is_capture(m)]
        capture_score = len(captures) / 15.0
        
        # 3. King Safety (Số lượng quân tấn công quanh ô Vua)
        our_king_sq = board.king(us)
        enemy_attackers = board.attackers(them, our_king_sq)
        king_pressure = len(enemy_attackers) / 4.0
        
        # 4. Material Tension (Tổng giá trị các quân đang bị đe dọa)
        tension = 0
        for sq, piece in board.piece_map().items():
            if piece.color == us:
                # Quân mình có bị quân nó nhìn thấy không?
                attackers = board.attackers(them, sq)
                if attackers:
                    defenders = board.attackers(us, sq)
                    if len(attackers) > len(defenders):
                        tension += piece.piece_type # Pawn=1, Knight=2, ..., King=6
                    elif len(attackers) > 0:
                        tension += 0.5 # Có sự dòm ngó
        tension_score = min(1.0, tension / 10.0)
        
        # Công thức tổng hợp (Weighted Average)
        tau = (mobility_score * 0.2) + (capture_score * 0.3) + (king_pressure * 0.2) + (tension_score * 0.3)
        return min(1.0, max(0.0, tau))

    @staticmethod
    def calculate_rho(board: chess.Board) -> float:
        """
        Tính toán rủi ro chân trời (Rho). 
        Đo lường khả năng thế cờ bị đảo ngược hoặc mất cân bằng đột ngột.
        """
        queens = len(board.pieces(chess.QUEEN, chess.WHITE)) + len(board.pieces(chess.QUEEN, chess.BLACK))
        rooks = len(board.pieces(chess.ROOK, chess.WHITE)) + len(board.pieces(chess.ROOK, chess.BLACK))
        rho = (queens * 0.3 + rooks * 0.15 + 0.2)
        
        return min(1.0, max(0.0, rho))

    @staticmethod
    def calculate_rs(board: chess.Board) -> float:
        """
        Tính toán độ phân giải (Resolution Score). 
        Tiến trình của ván đấu (Cờ tàn = RS cao).
        """
        piece_count = len(board.piece_map())
        rs = 1.0 - (piece_count / 32.0)
        return min(1.0, max(0.0, rs))

    @staticmethod
    def calculate_labels(board: chess.Board):
        return (
            PositionAnalyzer.calculate_tau(board),
            PositionAnalyzer.calculate_rho(board),
            PositionAnalyzer.calculate_rs(board)
        )
