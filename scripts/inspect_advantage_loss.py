import re

with open('matches/test_run_11_30s.pgn', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

games = text.split('[Event ')
for idx in [1, 5]:
    if idx < len(games):
        g = games[idx]
        print(f"=== Game {idx} Moves 18 to 28 ===")
        moves = re.findall(r'(\d+)\.\s+([^\s{]+)\s+\{([^}]+)\}\s+([^\s{]+)\s+\{([^}]+)\}', g)
        for num, w_mv, w_cmt, b_mv, b_cmt in moves:
            if 18 <= int(num) <= 28:
                print(f"{num}. {w_mv} {{{w_cmt}}} {b_mv} {{{b_cmt}}}")
        print()
