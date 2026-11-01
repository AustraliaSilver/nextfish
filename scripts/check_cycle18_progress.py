import re
import sys
import os

pgn_path = "matches/test_run_18_10s.pgn"
if not os.path.exists(pgn_path):
    print(f"PGN file not found: {pgn_path}")
    sys.exit(0)

with open(pgn_path, "r", encoding="utf-8", errors="ignore") as f:
    text = f.read()

games = re.split(r'\n(?=\[Event )', text)
games = [g for g in games if g.strip()]

nexus_wins = 0
seer_wins = 0
draws = 0
unterminated = 0
total_plies = 0
adjudications = 0
mates = 0

print(f"=== PROGRESS REPORT FOR CYCLE 18 ({len(games)} games played/recorded) ===")
for i, g in enumerate(games, 1):
    white = "NEXUS-CHESS" if 'White "NEXUS-CHESS"' in g else ("Seer" if 'White "Seer"' in g else "Unknown")
    black = "Seer" if white == "NEXUS-CHESS" else "NEXUS-CHESS"
    res_match = re.search(r'\[Result "([^"]+)"\]', g)
    result = res_match.group(1) if res_match else "*"

    ply_match = re.search(r'\[PlyCount "(\d+)"\]', g)
    plies = int(ply_match.group(1)) if ply_match else 0
    total_plies += plies

    term_match = re.search(r'\[Termination "([^"]+)"\]', g)
    term = term_match.group(1) if term_match else "Unknown"

    if "adjudication" in term.lower():
        adjudications += 1
    if "mate" in term.lower():
        mates += 1

    if result == "1-0":
        if white == "NEXUS-CHESS":
            nexus_wins += 1
        else:
            seer_wins += 1
    elif result == "0-1":
        if black == "NEXUS-CHESS":
            nexus_wins += 1
        else:
            seer_wins += 1
    elif result == "1/2-1/2":
        draws += 1
    else:
        unterminated += 1

    print(f"Game {i:2d}: {white:11s} vs {black:11s} | {result:7s} | {plies:3d} plies | {term}")

completed = nexus_wins + seer_wins + draws
if completed > 0:
    pts = nexus_wins + 0.5 * draws
    score_pct = (pts / completed) * 100.0
    print("=" * 60)
    print(f"Completed Games: {completed} | NEXUS: {nexus_wins} - Seer: {seer_wins} - Draws: {draws}")
    print(f"NEXUS Score: {pts:.1f}/{completed} ({score_pct:.1f}%)")
    print(f"Average plies: {total_plies / len(games):.1f} plies ({(total_plies / len(games)) / 2:.1f} full moves)")
    print(f"Adjudications: {adjudications}/{completed} ({adjudications/completed*100:.1f}%), Mates: {mates}/{completed} ({mates/completed*100:.1f}%)")
