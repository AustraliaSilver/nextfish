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
    
    # Extract move pairs and evaluations
    # Example format: 12. Nc3 {+0.45/14 0.3s} d5 {-0.20/12 0.3s}
    pattern = r'(\d+)\.\s+([^\s{]+)\s+\{([^}]+)\}(?:\s+([^\s{]+)\s+\{([^}]+)\})?'
    matches = re.findall(pattern, g)
    
    print(f"=== Game {idx} ({white} vs {black}) [{res}] ===")
    checkpoints = [10, 15, 20, 25, 30, 35, 40]
    for move_num, w_mv, w_cmt, b_mv, b_cmt in matches:
        m_int = int(move_num)
        if m_int in checkpoints:
            w_eval = w_cmt.split()[0] if w_cmt else '?'
            b_eval = b_cmt.split()[0] if b_cmt else '?'
            print(f"  Move {m_int:02d}: White={w_mv} ({w_eval}) | Black={b_mv} ({b_eval})")
    print()
