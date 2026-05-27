import os, json
if os.path.exists('D:/nextfish/spsa_tuning_history.jsonl'):
    with open('D:/nextfish/spsa_tuning_history.jsonl') as f:
        iters = [json.loads(l) for l in f]
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
        nw = nc.get('white', 0)
        nb = nc.get('black', 0)
        print(f'Iter {it:2d} [{ts}]: (+){p_wr:.0f}%({p_sc}) / (-){m_wr:.0f}%({m_sc}) => W={nw:.4f} B={nb:.4f}')
    state = json.load(open('D:/nextfish/spsa_tuning_state.json'))
    cur_w = state['parameters']['white_threshold']
    cur_b = state['parameters']['black_threshold']
    best_elo = state.get('best_elo', 0)
    best_params = state.get('best_params', {})
    print(f'Current center: White={cur_w:.4f}, Black={cur_b:.4f}')
    print(f'Best Elo: {best_elo}, Best params: {best_params}')
else:
    print('No history file yet')
