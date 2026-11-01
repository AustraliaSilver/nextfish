import sys
import re

pgn_path = sys.argv[1] if len(sys.argv) > 1 else 'matches/test_run_10_30s.pgn'

try:
    with open(pgn_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
except Exception as e:
    print(f"Error opening {pgn_path}: {e}")
    sys.exit(0)

games = content.split('[Event ')
print(f"Total games loaded: {len(games) - 1}")

nexus_wins = 0
seer_wins = 0
draws = 0
plies_list = []

for idx, g in enumerate(games[1:], 1):
    white = re.search(r'\[White "(.*?)"\]', g)
    black = re.search(r'\[Black "(.*?)"\]', g)
    res = re.search(r'\[Result "(.*?)"\]', g)
    w_name = white.group(1) if white else '?'
    b_name = black.group(1) if black else '?'
    r_val = res.group(1) if res else '?'
    
    # Extract termination message from end of game
    term_matches = re.findall(r'\{(.*?)\}', g)
    term_msg = term_matches[-1] if term_matches else '?'
    
    # Count plies
    # Moves look like: 1. e4 e5 2. Nf3 Nc6 ...
    move_tokens = re.findall(r'\b(?:[O0]-[O0](?:-[O0])?|[a-h][1-8]|[NBRQK][a-h1-8]?[a-h1-8]?x?[a-h][1-8](?:=[NBRQ])?)\b', g)
    ply_count = len(move_tokens)
    plies_list.append(ply_count)

    if r_val == '1/2-1/2':
        draws += 1
    elif (w_name == 'NEXUS-CHESS' and r_val == '1-0') or (b_name == 'NEXUS-CHESS' and r_val == '0-1'):
        nexus_wins += 1
    else:
        seer_wins += 1

    print(f"Game {idx:02d}: {w_name} vs {b_name} | Res: {r_val} | Plies: {ply_count} | Term: {term_msg}")

avg_plies = sum(plies_list) / len(plies_list) if plies_list else 0
print(f"\n--- MATCH SUMMARY ({len(games)-1} games) ---")
print(f"NEXUS-CHESS: +{nexus_wins} ={draws} -{seer_wins}")
print(f"Avg plies: {avg_plies:.1f}")
