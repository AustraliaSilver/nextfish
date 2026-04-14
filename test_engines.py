import chess
import chess.engine
import random
import json


def play_game(engine1_path, engine2_path, time_control=0.1, increment=0.01):
    engine1 = chess.engine.SimpleEngine.popen_uci(engine1_path)
    engine2 = chess.engine.SimpleEngine.popen_uci(engine2_path)

    board = chess.Board()
    moves = []

    while not board.is_game_over() and len(moves) < 200:
        engine = engine1 if board.turn == chess.WHITE else engine2
        result = engine.play(
            board, chess.engine.Limit(time=time_control)
        )
        board.push(result.move)
        moves.append(result.move)

    result = board.result()
    engine1.quit()
    engine2.quit()
    return result, moves


# Run 10 games
nextfish_wins = 0
stockfish_wins = 0
draws = 0

for i in range(10):
    if random.random() < 0.5:
        res, _ = play_game("./nextfish.exe", "./_c2e3d37_baseline.exe")
        if res == "1-0":
            nextfish_wins += 1
        elif res == "0-1":
            stockfish_wins += 1
        else:
            draws += 1
    else:
        res, _ = play_game("./_c2e3d37_baseline.exe", "./nextfish.exe")
        if res == "0-1":
            nextfish_wins += 1
        elif res == "1-0":
            stockfish_wins += 1
        else:
            draws += 1

print(f"Nextfish: {nextfish_wins}, Stockfish: {stockfish_wins}, Draws: {draws}")
score = (nextfish_wins + 0.5 * draws) / 10
print(f"Score: {score:.3f}")
if score > 0.5:
    print("Nextfish stronger")
elif score < 0.5:
    print("Stockfish stronger")
else:
    print("Equal")
