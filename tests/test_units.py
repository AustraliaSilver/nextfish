#!/usr/bin/env python3
"""Unit tests for the pure helper functions of the HARENN tooling.

Run with pytest, or directly:

    python3 tests/test_units.py
"""

import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import build_stockfish
import check_spsa
import harenn_fen


class TestCommonCxxFlags(unittest.TestCase):
    """build_stockfish.common_cxxflags"""

    def test_defaults(self):
        flags = build_stockfish.common_cxxflags()
        self.assertEqual(flags, ["-O3", "-DNDEBUG", "-std=c++17", "-fno-exceptions"])

    def test_sse_flag(self):
        self.assertIn("-msse4.2", build_stockfish.common_cxxflags(sse_flag="4.2"))
        self.assertNotIn("-msse4.2", build_stockfish.common_cxxflags())

    def test_no_sse_flag_is_falsy(self):
        # None and the empty string must both leave the flag out.
        self.assertNotIn("-msse", build_stockfish.common_cxxflags(sse_flag=None))
        self.assertNotIn("-msse", build_stockfish.common_cxxflags(sse_flag=""))

    def test_lto(self):
        flags = build_stockfish.common_cxxflags(enable_lto=True)
        self.assertIn("-flto=4", flags)
        self.assertIn("-flto-partition=one", flags)
        self.assertNotIn("-flto=4", build_stockfish.common_cxxflags())

    def test_unroll(self):
        self.assertIn("-funroll-loops", build_stockfish.common_cxxflags(enable_unroll=True))
        self.assertNotIn("-funroll-loops", build_stockfish.common_cxxflags())

    def test_omit_frame_pointer(self):
        self.assertIn("-fomit-frame-pointer",
                      build_stockfish.common_cxxflags(omit_frame_pointer=True))
        self.assertNotIn("-fomit-frame-pointer", build_stockfish.common_cxxflags())

    def test_static_link(self):
        self.assertIn("-static", build_stockfish.common_cxxflags(static_link=True))
        self.assertNotIn("-static", build_stockfish.common_cxxflags())

    def test_all_options_together(self):
        flags = build_stockfish.common_cxxflags(sse_flag="4.2", enable_lto=True,
                                               enable_unroll=True,
                                               omit_frame_pointer=True,
                                               static_link=True)
        for expected in ("-msse4.2", "-flto=4", "-flto-partition=one", "-funroll-loops",
                         "-fomit-frame-pointer", "-static"):
            self.assertIn(expected, flags)

    def test_base_flags_always_present(self):
        for base in ("-O3", "-DNDEBUG", "-std=c++17", "-fno-exceptions"):
            self.assertIn(base, build_stockfish.common_cxxflags(static_link=True))

    def test_returns_a_fresh_list(self):
        first = build_stockfish.common_cxxflags()
        first.append("-mutated")
        self.assertNotIn("-mutated", build_stockfish.common_cxxflags())


class TestMergeFlags(unittest.TestCase):
    """build_stockfish.merge_flags"""

    def test_merges_without_duplicates(self):
        merged = build_stockfish.merge_flags(["-O3", "-DNDEBUG"], ["-DNDEBUG", "-flto"])
        self.assertEqual(merged, ["-O3", "-DNDEBUG", "-flto"])

    def test_empty_base(self):
        self.assertEqual(build_stockfish.merge_flags([], ["-a", "-b"]), ["-a", "-b"])

    def test_empty_extra(self):
        self.assertEqual(build_stockfish.merge_flags(["-a"], []), ["-a"])

    def test_both_empty(self):
        self.assertEqual(build_stockfish.merge_flags([], []), [])

    def test_base_is_not_modified(self):
        base = ["-O3"]
        build_stockfish.merge_flags(base, ["-flto"])
        self.assertEqual(base, ["-O3"])

    def test_order_is_preserved(self):
        merged = build_stockfish.merge_flags(["-a", "-b", "-c"], ["-d", "-a", "-e"])
        self.assertEqual(merged, ["-a", "-b", "-c", "-d", "-e"])


class TestFenSideToMove(unittest.TestCase):
    """harenn_fen.fen_side_to_move"""

    def test_white_to_move(self):
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        self.assertEqual(harenn_fen.fen_side_to_move(fen), 0)

    def test_black_to_move(self):
        fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"
        self.assertEqual(harenn_fen.fen_side_to_move(fen), 1)

    def test_minimal_fen(self):
        self.assertEqual(harenn_fen.fen_side_to_move("8/8/8/8/8/8/8/8 w"), 0)

    def test_malformed_fen_raises(self):
        with self.assertRaises(ValueError):
            harenn_fen.fen_side_to_move("empty")

    def test_empty_string_raises(self):
        with self.assertRaises(ValueError):
            harenn_fen.fen_side_to_move("")


class TestFenMoveNumber(unittest.TestCase):
    """harenn_fen.fen_move_number"""

    def test_full_fen(self):
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        self.assertEqual(harenn_fen.fen_move_number(fen), 1)

    def test_later_move_number(self):
        fen = "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 4 4"
        self.assertEqual(harenn_fen.fen_move_number(fen), 4)

    def test_short_fen_returns_none(self):
        self.assertIsNone(harenn_fen.fen_move_number("8/8/8/8/8/8/8/8 w"))

    def test_empty_fen_returns_none(self):
        self.assertIsNone(harenn_fen.fen_move_number(""))

    def test_non_numeric_move_number_returns_none(self):
        fen = "8/8/8/8/8/8/8/8 w - - 0 x"
        self.assertIsNone(harenn_fen.fen_move_number(fen))


class TestGameIdFromName(unittest.TestCase):
    """harenn_fen.game_id_from_name"""

    def test_extracts_game_number(self):
        self.assertEqual(harenn_fen.game_id_from_name("w0_g7_b3_s42"), "g7")

    def test_returns_stem_when_no_game(self):
        self.assertEqual(harenn_fen.game_id_from_name("corpus"), "corpus")


class TestLoadHistory(unittest.TestCase):
    """check_spsa.load_history"""

    def test_reads_valid_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "history.jsonl")
            with open(path, "w") as f:
                f.write(json.dumps({"iteration": 1, "new_center": {"white_threshold": 823}}) + "\n")
                f.write(json.dumps({"iteration": 2, "new_center": {"black_threshold": 706}}) + "\n")
            iterations, skipped = check_spsa.load_history(path)
        self.assertEqual(len(iterations), 2)
        self.assertEqual(skipped, 0)
        self.assertEqual(iterations[0]["iteration"], 1)

    def test_skips_invalid_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "history.jsonl")
            with open(path, "w") as f:
                f.write(json.dumps({"iteration": 1}) + "\n")
                f.write("{ this is not json\n")
                f.write(json.dumps({"iteration": 2}) + "\n")
            iterations, skipped = check_spsa.load_history(path)
        self.assertEqual(len(iterations), 2)
        self.assertEqual(skipped, 1)

    def test_skips_blank_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "history.jsonl")
            with open(path, "w") as f:
                f.write("\n")
                f.write(json.dumps({"iteration": 7}) + "\n")
                f.write("   \n")
            iterations, skipped = check_spsa.load_history(path)
        self.assertEqual(len(iterations), 1)
        self.assertEqual(skipped, 0)

    def test_truncated_final_line_is_skipped(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "history.jsonl")
            with open(path, "w") as f:
                f.write(json.dumps({"iteration": 1}) + "\n")
                f.write('{"iteration": 2, "new_cen')
            iterations, skipped = check_spsa.load_history(path)
        self.assertEqual(len(iterations), 1)
        self.assertEqual(skipped, 1)

    def test_missing_file(self):
        iterations, skipped = check_spsa.load_history("/nonexistent/history.jsonl")
        self.assertEqual(iterations, [])
        self.assertEqual(skipped, 0)


try:
    import chess
except ImportError:  # pragma: no cover - python-chess is optional for the rest
    chess = None


@unittest.skipIf(chess is None, "python-chess is not installed")
class TestPositionAnalyzer(unittest.TestCase):
    """analyzer.PositionAnalyzer"""

    START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
    ENDGAME_FEN = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"

    def setUp(self):
        from analyzer import PositionAnalyzer
        self.analyzer = PositionAnalyzer

    def test_calculate_tau_in_range(self):
        board = chess.Board(self.START_FEN)
        tau = self.analyzer.calculate_tau(board)
        self.assertGreaterEqual(tau, 0.0)
        self.assertLessEqual(tau, 1.0)

    def test_calculate_tau_differs_between_positions(self):
        start = self.analyzer.calculate_tau(chess.Board(self.START_FEN))
        endgame = self.analyzer.calculate_tau(chess.Board(self.ENDGAME_FEN))
        self.assertNotEqual(start, endgame)

    def test_calculate_rho_in_range(self):
        board = chess.Board(self.START_FEN)
        rho = self.analyzer.calculate_rho(board)
        self.assertGreaterEqual(rho, 0.0)
        self.assertLessEqual(rho, 1.0)

    def test_calculate_rho_counts_queens_and_rooks(self):
        # Two queens and no rooks: 2 * 0.3 + 0.2 = 0.8
        board = chess.Board("8/8/8/3Q1Q2/8/8/8/4K2k w - - 0 1")
        self.assertAlmostEqual(self.analyzer.calculate_rho(board), 0.8, places=5)

    def test_calculate_rs_in_range(self):
        board = chess.Board(self.START_FEN)
        rs = self.analyzer.calculate_rs(board)
        self.assertGreaterEqual(rs, 0.0)
        self.assertLessEqual(rs, 1.0)

    def test_calculate_rs_is_higher_in_the_endgame(self):
        # rs = 1 - piece_count / 32, so a game that is far advanced (few
        # pieces left) has a higher resolution score than the start position.
        endgame = self.analyzer.calculate_rs(chess.Board(self.ENDGAME_FEN))
        start = self.analyzer.calculate_rs(chess.Board(self.START_FEN))
        self.assertGreater(endgame, start)
        self.assertAlmostEqual(start, 0.0, places=5)

    def test_calculate_labels_returns_three_values(self):
        board = chess.Board(self.START_FEN)
        labels = self.analyzer.calculate_labels(board)
        self.assertEqual(len(labels), 3)
        for value in labels:
            self.assertGreaterEqual(value, 0.0)
            self.assertLessEqual(value, 1.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
