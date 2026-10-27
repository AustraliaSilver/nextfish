import subprocess
import re
import sys
import os
import shutil
import argparse
import queue
import signal
import threading
import time
from pathlib import Path

ENGINE_OUTPUT_NAME = "stockfish.exe"

# Pre-compiled regular expressions for performance
SCORE_RE = re.compile(r"Score of NextfishImproved vs Stockfish:\s+(\d+)\s+-\s+(\d+)\s+-\s+(\d+)")
ELO_RE = re.compile(r"Elo difference:\s+([-+]?\d*\.\d+|\d+)")
LOS_RE = re.compile(r"LOS:\s+([-+]?\d*\.\d+|\d+)\s*%")
MATCH_TIMEOUT = 7200


def kill_process_tree(process):
    """Kill the cutechess process and all of its children."""
    try:
        if os.name == "nt":
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)],
                           capture_output=True, text=True)
        else:
            os.killpg(os.getpgid(process.pid), signal.SIGKILL)
    except Exception:
        try:
            process.kill()
        except Exception:
            pass


def clean_object_files(src_dir, output_name=ENGINE_OUTPUT_NAME):
    print("Cleaning up old object files...")
    count = 0
    for path in src_dir.rglob("*.o"):
        try:
            path.unlink()
            count += 1
        except Exception as e:
            print(f"Failed to delete {path}: {e}")

    # Only the known engine output is removed, never every *.exe in src/
    output_path = src_dir / output_name
    if output_path.exists():
        try:
            output_path.unlink()
            count += 1
        except Exception as e:
            print(f"Failed to delete {output_path}: {e}")

    print(f"Cleaned up {count} files.")

def compile_engine(pgo=False):
    print(f"=== Compiling Nextfish (PGO: {pgo}) ===")
    src_dir = Path("D:/nextfish/src")

    # Run Python-based clean
    clean_object_files(src_dir)

    # Set up compilation environment for MSYS2
    env = os.environ.copy()
    msys_bin = "C:\\msys64\\usr\\bin"
    if msys_bin not in env.get("PATH", ""):
        env["PATH"] = msys_bin + os.pathsep + env.get("PATH", "")

    if pgo:
        print("Running make profile-build...")
        build_cmd = ["make", "profile-build", "ARCH=x86-64-avx2", "COMP=mingw", "-j4"]
    else:
        print("Running make build...")
        build_cmd = ["make", "build", "ARCH=x86-64-avx2", "COMP=mingw", "-j4"]

    result = subprocess.run(build_cmd, cwd=src_dir, env=env)

    if result.returncode != 0:
        print("Error: Compilation failed!")
        return False

    # Find the compiled executable
    exe_name = ENGINE_OUTPUT_NAME
    compiled_exe = src_dir / exe_name
    target_exe = Path("D:/nextfish/nextfish_improved.exe")

    if compiled_exe.exists():
        shutil.copy(compiled_exe, target_exe)
        print(f"Success: Copied compiled engine to {target_exe}")
        return True
    else:
        print("Error: Compiled executable not found in src!")
        return False

def build_match_cmd(engine_path, stockfish_path, cutechess_path, book_path, tc, games, use_harenn, concurrency, pgn_file):
    if use_harenn:
        # Use HARENN options (only if engine supports them)
        cmd = [
            cutechess_path,
            "-engine", "name=NextfishImproved", f"cmd={engine_path}", "dir=D:\\nextfish",
                "option.EvalFile=D:\\nextfish\\nn-c288c895ea92.nnue",
                f"option.Use DEE/HARENN={'true' if use_harenn else 'false'}",
                "option.Use DEE Capture Ordering=false",
                "option.Use DEE Capture LMR=false",
                f"option.Use HARE Time Management={'true' if use_harenn else 'false'}",
                f"option.Use HARE Aspiration={'true' if use_harenn else 'false'}",
                "option.Use HARE Reduction=false",
                f"option.Hash={512}",
                f"option.Threads={2}",
                "proto=uci",
            "-engine", "name=Stockfish", f"cmd={stockfish_path}",
                f"option.Hash={512}",
                f"option.Threads={2}",
                "proto=uci",
            "-each", f"tc={tc}",
            "-rounds", str(max(1, games // 2)), "-repeat",
            "-concurrency", str(concurrency),
            "-openings", f"file={book_path}", "format=pgn", "order=random",
            "-pgnout", pgn_file
        ]
    else:
        # Use basic UCI options only
        cmd = [
            cutechess_path,
            "-engine", "name=NextfishImproved", f"cmd={engine_path}", "dir=D:\\nextfish",
                "option.EvalFile=D:\\nextfish\\nn-c288c895ea92.nnue",
                f"option.Hash={512}",
                f"option.Threads={2}",
                "proto=uci",
            "-engine", "name=Stockfish", f"cmd={stockfish_path}",
                f"option.Hash={512}",
                f"option.Threads={2}",
                "proto=uci",
            "-each", f"tc={tc}",
            "-rounds", str(max(1, games // 2)), "-repeat",
            "-concurrency", str(concurrency),
            "-openings", f"file={book_path}", "format=pgn", "order=random",
            "-pgnout", pgn_file
        ]
    return cmd

def run_process(cmd, on_line, timeout=MATCH_TIMEOUT):
    """Run cutechess, streaming its output, with a hard deadline."""
    process = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        start_new_session=(os.name != "nt"),
    )
    lines = queue.Queue()

    def pump():
        try:
            for line in process.stdout:
                lines.put(line)
        finally:
            lines.put(None)

    reader = threading.Thread(target=pump, daemon=True)
    reader.start()

    deadline = time.time() + timeout
    timed_out = False
    while True:
        remaining = deadline - time.time()
        if remaining <= 0:
            timed_out = True
            break
        try:
            line = lines.get(timeout=remaining)
        except queue.Empty:
            timed_out = True
            break
        if line is None:
            break
        on_line(line)

    if timed_out:
        print(f"\nTimeout after {timeout}s, killing cutechess process tree")
        kill_process_tree(process)
    process.wait()
    return process.returncode, timed_out

def run_cutechess(tc, games, pgn_file, concurrency=2, use_harenn=False, engine_path=None):
    print(f"\n=== Running Cute Chess Match (TC: {tc}, Games: {games}, Concurrency: {concurrency}, HARENN: {use_harenn}) ===")

    if engine_path is None:
        engine_path = "D:\\nextfish\\nextfish_improved.exe"
    stockfish_path = "D:\\nextfish\\stockfish18.exe"
    cutechess_path = "C:\\Program Files (x86)\\Cute Chess\\cutechess-cli.exe"
    book_path = "D:\\nextfish\\UHO_2022_8mvs_+110_+119.pgn"

    cmd = build_match_cmd(engine_path, stockfish_path, cutechess_path, book_path,
                          tc, games, use_harenn, concurrency, pgn_file)

    print(f"Command: {' '.join(cmd)}")

    wins = 0
    losses = 0
    draws = 0
    elo = 0.0
    los = 0.0

    def handle_line(line):
        nonlocal wins, losses, draws, elo, los
        print(line, end='')
        sys.stdout.flush()

        score_match = SCORE_RE.search(line)
        if score_match:
            wins = int(score_match.group(1))
            losses = int(score_match.group(2))
            draws = int(score_match.group(3))

        elo_match = ELO_RE.search(line)
        if elo_match:
            elo = float(elo_match.group(1))

        los_match = LOS_RE.search(line)
        if los_match:
            los = float(los_match.group(1))

    returncode, timed_out = run_process(cmd, handle_line)
    if timed_out:
        print("ERROR: cutechess timed out, results are incomplete")
        return wins, losses, draws, elo, los
    if returncode != 0:
        print(f"ERROR: cutechess failed with return code {returncode}")

    # An odd number of games cannot be covered by "-rounds N -repeat" (2N games),
    # so the remaining single game is played separately.
    if games % 2 == 1:
        extra_cmd = build_match_cmd(engine_path, stockfish_path, cutechess_path, book_path,
                                    tc, 2, use_harenn, concurrency, pgn_file)
        extra_cmd = extra_cmd[:extra_cmd.index("-rounds")] + ["-rounds", "1"] + \
            extra_cmd[extra_cmd.index("-rounds") + 3:]
        print(f"Playing the remaining single game for the odd game count {games}...")
        extra_returncode, extra_timed_out = run_process(extra_cmd, handle_line)
        if extra_timed_out or extra_returncode != 0:
            print("ERROR: the additional game could not be played")

    return wins, losses, draws, elo, los

def main():
    parser = argparse.ArgumentParser(description="Run Nextfish vs Stockfish benchmark")
    parser.add_argument("--pgo", action="store_true", help="Compile with PGO (profile-guided optimization)")
    parser.add_argument("--tc", type=str, default="2+0.1", help="Time control for the match (default: 2+0.1)")
    parser.add_argument("--games", "-g", type=int, default=20, help="Number of games to play (default: 20)")
    parser.add_argument("--concurrency", "-c", type=int, default=2, help="Number of concurrent games (default: 2)")
    parser.add_argument("--pgn", type=str, default="result_match.pgn", help="Output PGN filename (default: result_match.pgn)")
    parser.add_argument("--skip-compile", action="store_true", help="Skip compilation, run benchmark on existing executable")
    parser.add_argument("--harenn", action="store_true", help="Enable Use DEE/HARENN option in cutechess")
    parser.add_argument("--engine", type=str, help="Custom engine path (default: nextfish_improved.exe)")
    parser.add_argument("--timeout", type=int, default=MATCH_TIMEOUT, help="Match timeout in seconds")

    args = parser.parse_args()

    if not args.skip_compile:
        print(f"Compiling engine (PGO: {args.pgo})...")
        if not compile_engine(pgo=args.pgo):
            print("Compilation failed, exiting.")
            sys.exit(1)
    else:
        print("Skipping compilation as requested.")

    print("\nStarting benchmarks...")
    w, l, d, elo, los = run_cutechess(args.tc, args.games, args.pgn, args.concurrency, args.harenn, args.engine)

    print("\n" + "="*40)
    print(" BENCHMARK SUMMARY")
    print("="*40)
    print(f"Time Control: {args.tc}, Games: {args.games}")
    print(f"  Score: +{w} -{l} ={d} (Elo Diff: {elo:+.1f}, LOS: {los:.1f}%)")
    print("="*40)

if __name__ == "__main__":
    main()
