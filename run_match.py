"""
250-game match: new HARENN model vs Stockfish 18 (without HARENN).
Uses UHO 2022 book positions (8-move), 10+0.1 time control.
"""
import argparse
import json
import math
import os
import queue
import random
import re
import subprocess
import threading
import time

import chess

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))

DEFAULT_ENGINE = os.path.join(
    REPO_ROOT, "src", "stockfish.exe" if os.name == "nt" else "stockfish"
)
DEFAULT_BOOK = os.path.join(REPO_ROOT, "temp", "book_fens.json")

GAMES = 200
MOVE_TIMEOUT = 120
STARTUP_TIMEOUT = 60
QUIT_TIMEOUT = 3


class EngineError(RuntimeError):
    pass


class Engine:
    """UCI engine driven through a background reader thread."""

    def __init__(self, path, model_on=None, cwd=None):
        self.path = path
        self.proc = subprocess.Popen(
            [path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            cwd=cwd,
        )
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=self._pump, daemon=True)
        self.reader.start()
        self.send("uci")
        self._wait_for("uciok", STARTUP_TIMEOUT)
        # model_on=None leaves the options of the engine untouched, which is
        # what an external baseline engine needs.
        if model_on is not None:
            self.send(f"setoption name Use DEE/HARENN value {'true' if model_on else 'false'}")
        self.send("isready")
        self._wait_for("readyok", STARTUP_TIMEOUT)

    def _pump(self):
        try:
            for line in self.proc.stdout:
                self.lines.put(line)
        except Exception:
            pass
        finally:
            self.lines.put(None)

    def send(self, command):
        if self.proc.poll() is not None:
            raise EngineError(f"engine {self.path} has terminated")
        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()

    def _wait_for(self, token, timeout):
        deadline = time.time() + timeout
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise EngineError(f"timeout waiting for {token!r} from {self.path}")
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty:
                raise EngineError(f"timeout waiting for {token!r} from {self.path}")
            if line is None:
                raise EngineError(f"engine {self.path} closed its output while waiting for {token!r}")
            if token in line:
                return line

    def read_best_move(self, timeout=MOVE_TIMEOUT):
        deadline = time.time() + timeout
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                return None
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty:
                return None
            if line is None:
                return None
            m = re.search(r"bestmove\s+(\S+)", line)
            if m:
                return m.group(1)

    def close(self):
        try:
            self.send("quit")
        except Exception:
            pass
        try:
            self.proc.wait(timeout=QUIT_TIMEOUT)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        except Exception:
            pass
        for stream in (self.proc.stdin, self.proc.stdout, self.proc.stderr):
            try:
                if stream:
                    stream.close()
            except Exception:
                pass
        return self.proc.returncode


def elo_from_score(score, games):
    if games <= 0:
        return 0.0
    if score <= 0.0:
        return -1000.0
    if score >= 1.0:
        return 1000.0
    return -400.0 * math.log10((1.0 / score) - 1.0)


def elo_error(score, games):
    if games <= 0:
        return 0.0
    variance = score * (1.0 - score)
    if variance <= 0.0:
        return float("inf")
    return 173.7 / math.sqrt(games * variance)


def result_for_model(result, model_white):
    """board.result() is White's perspective; convert it to the model's perspective.

    board.result() returns "*" when the game is still running, which happens
    when it hit the ply limit without a mate, a stalemate, a repetition or a
    fifty move draw. Such a game counts as a draw. Treating it as anything
    else would either drop the game from the tally or, worse, count it as a
    win for the model.
    """
    if result in ("1/2-1/2", "*"):
        return "1/2-1/2"
    if model_white:
        return result
    return "0-1" if result == "1-0" else "1-0"


def run_match(engine_path, book_path, games, movetime=None, tc=None, engine2=None):
    with open(book_path) as f:
        book = json.load(f)
    random.shuffle(book)
    print(f"Loaded {len(book)} book positions", flush=True)

    assert len(book) % 2 == 0, f"book must contain an even number of positions, got {len(book)}"

    if movetime is not None:
        go_cmd = f"go movetime {movetime}"
        tc_desc = f"movetime {movetime}ms"
    else:
        wtime, btime, winc, binc = tc
        go_cmd = f"go wtime {wtime} btime {btime} winc {winc} binc {binc}"
        tc_desc = f"{wtime}+{winc / 10:.1f}s"
    print(f"Time control: {tc_desc}", flush=True)

    results = {"1-0": 0, "0-1": 0, "1/2-1/2": 0}
    aborted = 0
    counted = 0
    games_as_white = 0
    games_as_black = 0
    total_ply = 0

    for g in range(games):
        fen = book[g % len(book)]
        model_white = (g % 2 == 0)
        if model_white:
            games_as_white += 1
        else:
            games_as_black += 1

        try:
            pw = Engine(engine_path, model_white)
            if engine2:
                # The opponent is an external baseline, its options are left alone.
                pb = Engine(engine2, None)
            else:
                pb = Engine(engine_path, not model_white)
        except (OSError, EngineError) as e:
            print(f"G{g+1:3d}/{games} could not start the engines ({e})", flush=True)
            raise SystemExit(1)

        moves = []
        side = 0
        game_aborted = False
        t0 = time.time()

        for ply in range(500):
            p = pw if side == 0 else pb
            try:
                p.send(f"position fen {fen} moves {' '.join(moves)}")
                p.send(go_cmd)
            except EngineError as e:
                print(f"G{g+1:3d}/{games} aborted after {len(moves)} ply ({e})", flush=True)
                game_aborted = True
                break

            bm = p.read_best_move()
            if bm is None:
                game_aborted = True
                print(f"G{g+1:3d}/{games} aborted after {len(moves)} ply (engine died or timed out)", flush=True)
                break
            if bm == "(none)":
                break
            moves.append(bm)
            side = 1 - side

        pw.close()
        pb.close()

        duration = time.time() - t0
        total_ply += len(moves)

        if game_aborted:
            aborted += 1
        else:
            counted += 1
            try:
                board = chess.Board(fen)
                for m in moves:
                    board.push_uci(m)
                result = board.result(claim_draw=True)
            except Exception:
                result = "1/2-1/2"
            result = result_for_model(result, model_white)
            results[result] = results.get(result, 0) + 1

        w, l, d = results.get("1-0", 0), results.get("0-1", 0), results.get("1/2-1/2", 0)
        n = w + l + d
        score = (w + 0.5 * d) / n if n else 0.0
        score_pct = score * 100
        elo = elo_from_score(score, n)
        elo_err = elo_error(score, n)

        side_str = f"HARENN={'White' if model_white else 'Black'}"
        print(f"G{g+1:3d}/{games} {('ABORT' if game_aborted else 'OK'):7s} ply={len(moves):3d} {side_str} "
              f"[{w}W-{l}L-{d}D] {score_pct:.1f}% {elo:+.1f}±{elo_err:.0f}El t={duration:.0f}s", flush=True)

    assert games_as_white == games_as_black, (
        f"unbalanced colours: {games_as_white} white vs {games_as_black} black games"
    )

    w, l, d = results.get("1-0", 0), results.get("0-1", 0), results.get("1/2-1/2", 0)
    n = w + l + d
    # Every game has to end up either in the result table or in the aborted
    # counter. A game that is counted nowhere would silently shrink the
    # sample, so it is reported instead.
    if n + aborted != games:
        print(f"::warning::{games - n - aborted} game(s) were neither scored nor aborted",
              flush=True)
    print("\n" + "=" * 60, flush=True)
    print(f"RESULTS: {w}W - {l}L - {d}D in {n} games", flush=True)
    print(f"Colours: {games_as_white} white / {games_as_black} black", flush=True)
    print(f"Aborted games: {aborted}", flush=True)
    if n:
        score = (w + 0.5 * d) / n
        score_pct = score * 100
        elo = elo_from_score(score, n)
        elo_err = elo_error(score, n)
        print(f"Score: {score_pct:.1f}%", flush=True)
        print(f"Elo: {elo:+.1f} ± {elo_err:.1f}", flush=True)
        print(f"Avg ply: {total_ply / n:.1f}", flush=True)
    return results, aborted


def main():
    parser = argparse.ArgumentParser(description="Run a HARENN vs Stockfish match")
    parser.add_argument("--engine", default=DEFAULT_ENGINE, help="Path to the engine binary")
    parser.add_argument("--book", default=DEFAULT_BOOK, help="Path to the book FEN file")
    parser.add_argument("--games", "-g", type=int, default=GAMES, help="Number of games to play")
    parser.add_argument("--movetime", type=int, default=None,
                        help="Fixed time per move in milliseconds. Overrides the time control.")
    parser.add_argument("--tc", default="10000,10000,100,100",
                        help="Time control as wtime,btime,winc,binc in milliseconds")
    parser.add_argument("--engine2", default=None,
                        help="Second engine binary used as the opponent. "
                             "Without it the engine plays itself with HARENN on and off.")
    args = parser.parse_args()

    tc = tuple(int(x) for x in args.tc.split(","))
    assert len(tc) == 4, "--tc must be wtime,btime,winc,binc"

    run_match(args.engine, args.book, args.games, movetime=args.movetime, tc=tc,
               engine2=args.engine2)


if __name__ == "__main__":
    main()
