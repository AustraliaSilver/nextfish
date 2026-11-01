import sys
import re

pgn_path = sys.argv[1] if len(sys.argv) > 1 else 'matches/test_run_10_30s.pgn'

with open(pgn_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

games = text.split('[Event ')
for idx, g in enumerate(games[1:], 1):
    white = re.search(r'\[White "(.*?)"\]', g).group(1)
    black = re.search(r'\[Black "(.*?)"\]', g).group(1)
    res = re.search(r'\[Result "(.*?)"\]', g).group(1)
    
    # Extract moves
    # Cutechess PGN contains comments with eval/time: 1. e4 {+0.15/12 0.3s} e5 ...
    # Let's extract the last 6 moves with comments
    moves = re.findall(r'(\d+\.+|[a-h][1-8]|[NBRQK][a-h1-8]?[a-h1-8]?x?[a-h][1-8](?:=[NBRQ])?|[O0]-[O0](?:-[O0])?|\{.*?\})', g)
    
    # Filter out header comments
    move_tokens = [m for m in moves if not m.startswith('{') or ('/' in m or 'mate' in m or 'draw' in m or 'lose' in m or 'win' in m or 's' in m)]
    last_tokens = ' '.join(move_tokens[-20:])
    print(f"=== Game {idx} ({white} vs {black}) [{res}] ===")
    print(f"Ending moves: {last_tokens}\n")
