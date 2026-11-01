import re

with open('matches/test_run_11_30s.pgn', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

games = text.split('[Event ')
for idx in [2, 6]:
    if idx < len(games):
        g = games[idx]
        print(f"=== Game {idx} ===")
        # Print moves 1 to 20
        moves = re.findall(r'\d+\.\s+[^\s{]+(?:\s+\{[^}]+\})?\s+[^\s{]+(?:\s+\{[^}]+\})?', g)
        for m in moves[:20]:
            print(m)
        print()
