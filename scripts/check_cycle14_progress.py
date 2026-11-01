import chess.pgn
import io

with open('matches/test_run_14_30s.pgn', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

pgn_io = io.StringIO(text)
game_idx = 1
adjudications = 0
mates = 0
plies = []

while True:
    game = chess.pgn.read_game(pgn_io)
    if not game:
        break
    term = game.headers.get('Termination', 'Unknown')
    moves = list(game.mainline_moves())
    plies.append(len(moves))
    if 'adjudication' in term.lower():
        adjudications += 1
    elif 'mate' in term.lower():
        mates += 1
    w = game.headers.get("White")
    b = game.headers.get("Black")
    res = game.headers.get("Result")
    print(f"Game {game_idx}: {w} vs {b} | {res} | {len(moves)} plies | {term}")
    game_idx += 1

if plies:
    print(f"\nTotal games: {len(plies)}, Avg plies: {sum(plies)/len(plies):.1f}, Adjudications: {adjudications}/{len(plies)} ({adjudications*100/len(plies):.1f}%), Mates: {mates}/{len(plies)} ({mates*100/len(plies):.1f}%)")
