import re
import os
import sys

pgn_path = "matches/test_run_17_30s.pgn"
if not os.path.exists(pgn_path):
    print("PGN file does not exist yet.")
    sys.exit(0)

with open(pgn_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

games = text.split('[Event "')
game_list = [g for g in games if g.strip()]

plies_list = []
mates = 0
adjudications = 0
nexus_wins = 0
seer_wins = 0
draws = 0

for i, g in enumerate(game_list, 1):
    white_m = re.search(r'\[White "([^"]+)"\]', g)
    black_m = re.search(r'\[Black "([^"]+)"\]', g)
    res_m = re.search(r'\[Result "([^"]+)"\]', g)
    term_m = re.search(r'\[Termination "([^"]+)"\]', g)
    ply_m = re.search(r'\[PlyCount "([^"]+)"\]', g)

    white = white_m.group(1) if white_m else "Unknown"
    black = black_m.group(1) if black_m else "Unknown"
    res = res_m.group(1) if res_m else "*"
    term = term_m.group(1) if term_m else "Unknown"
    ply = int(ply_m.group(1)) if ply_m else 0

    plies_list.append(ply)

    if res == "1/2-1/2":
        draws += 1
    elif res == "1-0":
        if white == "NEXUS-CHESS":
            nexus_wins += 1
        else:
            seer_wins += 1
    elif res == "0-1":
        if black == "NEXUS-CHESS":
            nexus_wins += 1
        else:
            seer_wins += 1

    if "mate" in term.lower():
        mates += 1
    elif "adjudication" in term.lower():
        adjudications += 1

    print(f"Game {i:2d}: {white:11s} vs {black:11s} | {res} | {ply:3d} plies | {term}")

n = len(game_list)
if n > 0:
    avg_ply = sum(plies_list) / n
    print(f"\nTotal games: {n} | NEXUS: {nexus_wins} - Seer: {seer_wins} - Draws: {draws}")
    print(f"Average plies: {avg_ply:.1f} plies ({avg_ply/2:.1f} full moves)")
    print(f"Adjudications: {adjudications}/{n} ({adjudications/n*100:.1f}%), Mates: {mates}/{n} ({mates/n*100:.1f}%)")
