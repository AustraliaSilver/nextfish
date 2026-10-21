import os
import json

HISTORY_FILE = os.environ.get("SPSA_HISTORY_FILE", "spsa_tuning_history.jsonl")
STATE_FILE = os.environ.get("SPSA_STATE_FILE", "spsa_tuning_state.json")

if os.path.exists(HISTORY_FILE):
    iters = []
    with open(HISTORY_FILE) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                iters.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    print(f'Total iterations done: {len(iters)}')
    for d in iters:
        it = d['iteration']
        nc = d.get('new_center', {})
        p = d.get('plus_candidate', {})
        m = d.get('minus_candidate', {})
        ts = d.get('timestamp', 'N/A')[:16]
        p_wr = p.get('win_rate', 0) * 100
        m_wr = m.get('win_rate', 0) * 100
        p_sc = p.get('score', p.get('games', '?'))
        m_sc = m.get('score', m.get('games', '?'))
        nw = nc.get('white_threshold', 0)
        nb = nc.get('black_threshold', 0)
        print(f'Iter {it:2d} [{ts}]: (+){p_wr:.0f}%({p_sc}) / (-){m_wr:.0f}%({m_sc}) '
              f'=> W={nw} ({nw / 1000:.4f}) B={nb} ({nb / 1000:.4f})')
    if os.path.exists(STATE_FILE):
        with open(STATE_FILE) as f:
            state = json.load(f)
        cur_w = state['parameters']['white_threshold']
        cur_b = state['parameters']['black_threshold']
        best_elo = state.get('best_elo', 0)
        best_params = state.get('best_params', {})
        print(f'Current center: White={cur_w} ({cur_w / 1000:.4f}), Black={cur_b} ({cur_b / 1000:.4f})')
        print(f'Best Elo: {best_elo}, Best params: {best_params}')
    else:
        print(f'No state file at {STATE_FILE}')
else:
    print('No history file yet')
