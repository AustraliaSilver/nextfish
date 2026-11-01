import sys
sys.path.append('Neuron Network')
import struct
import numpy as np

with open('Neuron Network/nextfish_v15.harenn', 'rb') as f:
    magic = f.read(4)
    mean, std = struct.unpack('<ff', f.read(8))
    # fc1: 768 x 1024
    r1, c1 = struct.unpack('<ii', f.read(8))
    w1 = np.frombuffer(f.read(r1 * c1 * 2), dtype=np.int16).reshape(r1, c1).astype(np.float32) / 128.0
    b1 = np.frombuffer(f.read(c1 * 4), dtype=np.int32).astype(np.float32) / 128.0
    # fc2: 512 x 1024
    r2, c2 = struct.unpack('<ii', f.read(8))
    w2 = np.frombuffer(f.read(r2 * c2 * 2), dtype=np.int16).reshape(r2, c2).astype(np.float32) / 128.0
    b2 = np.frombuffer(f.read(r2 * 4), dtype=np.int32).astype(np.float32) / 16384.0
    # eval head: 1 x 512
    rh, ch = struct.unpack('<ii', f.read(8))
    w_eval = np.frombuffer(f.read(rh * ch * 2), dtype=np.int16).reshape(rh, ch).astype(np.float32) / 128.0
    b_eval = np.frombuffer(f.read(rh * 4), dtype=np.int32).astype(np.float32) / 2097152.0

fen = 'r2n1rk1/p1p1pp2/1p4p1/2pP1qNp/4Q3/5NP1/P3PP1P/R5K1 w - - 1 20'
PIECE_MAP = {'P': 0, 'N': 1, 'B': 2, 'R': 3, 'Q': 4, 'K': 5, 'p': 6, 'n': 7, 'b': 8, 'r': 9, 'q': 10, 'k': 11}
board_part = fen.split()[0]
x = np.zeros(768, dtype=np.float32)
for rank_fen, row in enumerate(board_part.split('/')):
    file = 0
    for ch in row:
        if ch.isdigit():
            file += int(ch)
        else:
            sf_sq = (7 - rank_fen) * 8 + file
            py_sq = sf_sq ^ 56
            py_pc = PIECE_MAP[ch]
            x[py_sq * 12 + py_pc] = 1.0
            file += 1

h1 = np.clip(np.dot(x, w1) + b1, 0.0, 1.0)
h2 = np.clip(np.dot(h1, w2.T) + b2, 0.0, 1.0)
raw_eval = float((np.dot(h2, w_eval.T) + b_eval).squeeze())
eval_pawns = raw_eval * std + mean
print(f'NN Raw Eval: {raw_eval:.4f}')
print(f'NN Eval in pawns: {eval_pawns:.4f}')
print(f'NN Eval in centipawns: {eval_pawns * 100:.1f} cp')
