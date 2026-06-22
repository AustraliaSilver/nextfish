"""
250-game match: new HARENN model vs Stockfish 18 (without HARENN).
Uses UHO 2022 book positions (8-move), 10+0.1 time control.
"""
import subprocess, re, time, random, os, json
import chess

ENGINE = r"D:\nextfish\src\stockfish.exe"
BOOK_FILE = r"D:\nextfish\temp\book_fens.json"
GAMES = 200

with open(BOOK_FILE) as f:
    book = json.load(f)
random.shuffle(book)
print(f"Loaded {len(book)} book positions", flush=True)

results = {"1-0": 0, "0-1": 0, "1/2-1/2": 0}
total_ply = 0

def run_engine(side, model_on):
    p = subprocess.Popen(
        [ENGINE],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, bufsize=1, cwd=r"D:\nextfish\src"
    )
    p.stdin.write("uci\n")
    p.stdin.flush()
    while True:
        line = p.stdout.readline()
        if "uciok" in line:
            break
    p.stdin.write(f"setoption name Use DEE/HARENN value {model_on}\n")
    p.stdin.write("isready\n")
    p.stdin.flush()
    while True:
        line = p.stdout.readline()
        if "readyok" in line:
            break
    return p

def read_best_move(p, timeout=120):
    while True:
        line = p.stdout.readline()
        if not line:
            return None
        m = re.search(r"bestmove\s+(\S+)", line)
        if m:
            return m.group(1)

for g in range(GAMES):
    fen = book[g % len(book)]
    model_white = (g % 2 == 0)
    side_label = "HARENN" if model_white else "SF18"
    
    if model_white:
        pw = run_engine("white", "true")
        pb = run_engine("black", "false")
    else:
        pw = run_engine("white", "false")
        pb = run_engine("black", "true")

    moves = []
    side = 0
    t0 = time.time()

    for ply in range(500):
        p = pw if side == 0 else pb
        p.stdin.write(f"position fen {fen} moves {' '.join(moves)}\n")
        p.stdin.write("go wtime 10000 btime 10000 winc 100 binc 100\n")
        p.stdin.flush()
        
        bm = read_best_move(p)
        if bm is None or bm == "(none)":
            break
        moves.append(bm)
        side = 1 - side

    for p in [pw, pb]:
        try:
            p.stdin.write("quit\n")
            p.stdin.flush()
            p.wait(timeout=3)
        except:
            p.kill()

    try:
        board = chess.Board(fen)
        for m in moves:
            board.push_uci(m)
        result = board.result(claim_draw=True)
    except:
        result = "1/2-1/2"

    duration = time.time() - t0
    total_ply += len(moves)
    results[result] = results.get(result, 0) + 1

    w, l, d = results.get("1-0", 0), results.get("0-1", 0), results.get("1/2-1/2", 0)
    n = w + l + d
    score_pct = (w + 0.5 * d) / n * 100 if n else 0
    elo = 400 * (w - l) / n if n else 0
    elo_err = 400 * (w + l) ** 0.5 / n if n else 0
    
    side_str = f"HARENN={side_label}"
    print(f"G{g+1:3d}/{GAMES} {result:7s} ply={len(moves):3d} {side_str} "
          f"[{w}W-{l}L-{d}D] {score_pct:.1f}% {elo:+.1f}±{elo_err:.0f}El t={duration:.0f}s", flush=True)

n = w + l + d
print("\n" + "=" * 60, flush=True)
print(f"RESULTS: {w}W - {l}L - {d}D in {n} games", flush=True)
if n:
    score_pct = (w + 0.5 * d) / n * 100
    elo = 400 * (w - l) / n
    elo_err = 400 * (w + l) ** 0.5 / n
    print(f"Score: {score_pct:.1f}%", flush=True)
    print(f"Elo: {elo:+.1f} ± {elo_err:.1f}", flush=True)
    print(f"Avg ply: {total_ply / n:.1f}", flush=True)
