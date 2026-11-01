import os
import shutil
import subprocess
import sys

print("=== LAUNCHING CYCLE 16 (builds/nexus-v16.exe -> builds/nexus-chess.exe) ===")

src_bin = "builds/nexus-v16.exe"
dst_bin = "builds/nexus-chess.exe"

if not os.path.exists(src_bin):
    print(f"ERROR: {src_bin} does not exist!")
    sys.exit(1)

try:
    shutil.copyfile(src_bin, dst_bin)
    print(f"Successfully copied {src_bin} to {dst_bin} ({os.path.getsize(dst_bin)} bytes)")
except Exception as e:
    print(f"ERROR copying binary: {e}")
    sys.exit(1)

cmd = [
    "cutechess/cutechess-cli.exe",
    "-engine", "cmd=builds/nexus-chess.exe", "name=NEXUS-CHESS", "proto=xboard", "initstr=memory 64",
    "-engine", "cmd=seer/seer_v2.8_x64_avx2_popcnt.exe", "name=Seer", "proto=uci", "option.Hash=64",
    "-each", "tc=30+0.3", "timemargin=50",
    "-openings", "file=pgn/UHO_2022_8mvs_+110_+119.pgn", "format=pgn", "order=random",
    "-repeat",
    "-games", "50",
    "-concurrency", "2",
    "-pgnout", "matches/test_run_16_30s.pgn",
    "-draw", "movenumber=40", "movecount=8", "score=10",
    "-resign", "movecount=5", "score=1000"
]

print("Starting cutechess-cli for Cycle 16...")
print("Command:", " ".join(cmd))
sys.stdout.flush()

proc = subprocess.run(cmd)
print(f"Cutechess finished with returncode: {proc.returncode}")
