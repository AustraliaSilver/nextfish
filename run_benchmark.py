import subprocess
import re
import sys
import os
import shutil
import argparse
from pathlib import Path

def clean_object_files(src_dir):
    print("Cleaning up old object files...")
    count = 0
    for path in src_dir.rglob("*.o"):
        try:
            path.unlink()
            count += 1
        except Exception as e:
            print(f"Failed to delete {path}: {e}")
            
    # Also delete existing executables in src
    for path in src_dir.glob("*.exe"):
        try:
            path.unlink()
            count += 1
        except Exception as e:
            print(f"Failed to delete {path}: {e}")
            
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
        
    result = subprocess.run(build_cmd, cwd=src_dir, shell=True, env=env)
    
    if result.returncode != 0:
        print("Error: Compilation failed!")
        return False
        
    # Find the compiled executable
    exe_name = "stockfish.exe"
    compiled_exe = src_dir / exe_name
    target_exe = Path("D:/nextfish/nextfish_improved.exe")
    
    if compiled_exe.exists():
        shutil.copy(compiled_exe, target_exe)
        print(f"Success: Copied compiled engine to {target_exe}")
        return True
    else:
        print("Error: Compiled executable not found in src!")
        return False

def run_cutechess(tc, games, pgn_file, concurrency=2, use_harenn=False):
    print(f"\n=== Running Cute Chess Match (TC: {tc}, Games: {games}, Concurrency: {concurrency}, HARENN: {use_harenn}) ===")
    
    engine_path = "D:\\nextfish\\nextfish_improved.exe"
    stockfish_path = "C:\\Users\\Admin\\Downloads\\stockfish-windows-x86-64-avx2\\stockfish\\stockfish-windows-x86-64-avx2.exe"
    cutechess_path = "C:\\Program Files (x86)\\Cute Chess\\cutechess-cli.exe"
    book_path = "D:\\nextfish\\UHO_2022_8mvs_+110_+119.pgn"
    
    cmd = [
        cutechess_path,
        "-engine", "name=NextfishImproved", f"cmd={engine_path}", "dir=D:\\nextfish",
            "option.EvalFile=D:\\nextfish\\nn-c288c895ea92.nnue",
            "option.Move Overhead=150",
            f"option.Use DEE/HARENN={'true' if use_harenn else 'false'}",
            f"option.Use DEE Capture Ordering={'true' if use_harenn else 'false'}",
            "option.Use HARE Time Management=true",
            "option.Hash=256",
            "proto=uci",
        "-engine", "name=Stockfish", f"cmd={stockfish_path}",
            "option.Move Overhead=150",
            "option.Hash=128",
            "proto=uci",
        "-each", f"tc={tc}",
        "-rounds", str(games // 2), "-repeat",
        "-concurrency", str(concurrency),
        "-openings", f"file={book_path}", "format=pgn", "order=random",
        "-pgnout", pgn_file
    ]
    
    print(f"Command: {' '.join(cmd)}")
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    
    wins = 0
    losses = 0
    draws = 0
    elo = 0.0
    los = 0.0
    
    for line in process.stdout:
        print(line, end='')
        sys.stdout.flush()
        
        # Parse score updates
        score_match = re.search(r"Score of NextfishImproved vs Stockfish:\s+(\d+)\s+-\s+(\d+)\s+-\s+(\d+)", line)
        if score_match:
            wins = int(score_match.group(1))
            losses = int(score_match.group(2))
            draws = int(score_match.group(3))
            
        elo_match = re.search(r"Elo difference:\s+([-+]?\d*\.\d+|\d+)", line)
        if elo_match:
            elo = float(elo_match.group(1))
            
        los_match = re.search(r"LOS:\s+([-+]?\d*\.\d+|\d+)\s*%", line)
        if los_match:
            los = float(los_match.group(1))
            
    process.wait()
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
    
    args = parser.parse_args()
    
    if not args.skip_compile:
        print(f"Compiling engine (PGO: {args.pgo})...")
        if not compile_engine(pgo=args.pgo):
            print("Compilation failed, exiting.")
            sys.exit(1)
    else:
        print("Skipping compilation as requested.")
        
    print("\nStarting benchmarks...")
    w, l, d, elo, los = run_cutechess(args.tc, args.games, args.pgn, args.concurrency, args.harenn)
    
    print("\n" + "="*40)
    print(" BENCHMARK SUMMARY")
    print("="*40)
    print(f"Time Control: {args.tc}, Games: {args.games}")
    print(f"  Score: +{w} -{l} ={d} (Elo Diff: {elo:+.1f}, LOS: {los:.1f}%)")
    print("="*40)

if __name__ == "__main__":
    main()
