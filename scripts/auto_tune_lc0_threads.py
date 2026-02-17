#!/usr/bin/env python3
"""
Auto-tune Lc0Policy_Threads default in src/engine.cpp using fastchess self-play.

This script compares candidate thread counts against a baseline thread count
using the same engine binary, then updates the default UCI option value if a
better candidate is found.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path


@dataclass
class MatchResult:
    candidate: int
    wins: int
    losses: int
    draws: int
    score: float
    games: int
    elo: float
    elo_ci: float
    los: float
    draw_ratio: float


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Tune Lc0Policy_Threads via fastchess.")
    parser.add_argument("--root", default=str(root))
    parser.add_argument("--engine", default="./nextfish")
    parser.add_argument("--fastchess", default="./fastchess")
    parser.add_argument("--book", default="UHO_2022_8mvs_+110_+119.pgn")
    parser.add_argument("--tc", default="10+0.1")
    parser.add_argument("--rounds", type=int, default=8, help="Fastchess rounds (2 games each).")
    parser.add_argument("--concurrency", type=int, default=2)
    parser.add_argument("--base", type=int, default=2, help="Baseline Lc0Policy_Threads value.")
    parser.add_argument("--candidates", default="1,2,3,4")
    parser.add_argument("--timeout", type=int, default=2400)
    parser.add_argument("--out-dir", default="autotune_results")
    return parser.parse_args()


def score_to_elo(score: float) -> float:
    if score <= 0.0:
        return -1000.0
    if score >= 1.0:
        return 1000.0
    return -400.0 * math.log10((1.0 / score) - 1.0)


def parse_match_output(output: str, candidate: int) -> MatchResult:
    score_all = list(re.finditer(r"Score of .*?:\s*(\d+)\s*-\s*(\d+)\s*-\s*(\d+)\s*\[([0-9.]+)\]\s*(\d+)", output))
    if not score_all:
        raise RuntimeError("Could not parse fastchess score line.")
    w, l, d, s, g = score_all[-1].groups()

    elo_all = list(
        re.finditer(
            r"Elo difference:\s*([\-+0-9.infna]+)\s*\+/-\s*([0-9.infna]+),\s*LOS:\s*([0-9.]+)\s*%,\s*DrawRatio:\s*([0-9.]+)\s*%",
            output,
        )
    )
    if elo_all:
        e, ci, los, dr = elo_all[-1].groups()
        elo = float(e.replace("+", "")) if e not in ("inf", "+inf", "-inf", "nan") else score_to_elo(float(s))
        elo_ci = float(ci) if ci not in ("nan", "inf", "+inf", "-inf") else float("nan")
        los_v = float(los)
        dr_v = float(dr)
    else:
        elo = score_to_elo(float(s))
        elo_ci = float("nan")
        los_v = float("nan")
        dr_v = (100.0 * int(d)) / max(1, int(g))

    return MatchResult(
        candidate=candidate,
        wins=int(w),
        losses=int(l),
        draws=int(d),
        score=float(s),
        games=int(g),
        elo=elo,
        elo_ci=elo_ci,
        los=los_v,
        draw_ratio=dr_v,
    )


def run_match(args: argparse.Namespace, candidate: int) -> MatchResult:
    cmd = [
        args.fastchess,
        "-engine",
        f"cmd={args.engine}",
        "name=Candidate",
        f"option.Lc0Policy_Threads={candidate}",
        "-engine",
        f"cmd={args.engine}",
        "name=Baseline",
        f"option.Lc0Policy_Threads={args.base}",
        "-each",
        "proto=uci",
        f"tc={args.tc}",
        "-rounds",
        str(args.rounds),
        "-games",
        "2",
        "-repeat",
        "-concurrency",
        str(args.concurrency),
        "-openings",
        f"file={args.book}",
        "format=pgn",
        "order=random",
    ]

    print(f"[RUN] candidate={candidate} vs base={args.base} rounds={args.rounds}")
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout)
    output = (proc.stdout or "") + (proc.stderr or "")
    if "Score of" not in output:
        raise RuntimeError(f"fastchess did not return score for candidate={candidate}")
    result = parse_match_output(output, candidate)
    print(
        f"[RES] candidate={candidate} WLD={result.wins}-{result.losses}-{result.draws} "
        f"score={result.score:.3f} elo={result.elo:+.2f}"
    )
    return result


def read_current_default(engine_cpp: Path) -> int:
    text = engine_cpp.read_text(encoding="utf-8")
    m = re.search(r'options\.add\("Lc0Policy_Threads",\s*Option\((\d+),\s*1,\s*128,', text)
    if not m:
        raise RuntimeError("Cannot find Lc0Policy_Threads default in src/engine.cpp")
    return int(m.group(1))


def write_new_default(engine_cpp: Path, new_value: int) -> bool:
    text = engine_cpp.read_text(encoding="utf-8")
    new_text, count = re.subn(
        r'(options\.add\("Lc0Policy_Threads",\s*Option\()(\d+)(,\s*1,\s*128,)',
        rf"\g<1>{new_value}\g<3>",
        text,
        count=1,
    )
    if count != 1:
        raise RuntimeError("Failed to update Lc0Policy_Threads default in src/engine.cpp")
    if new_text == text:
        return False
    engine_cpp.write_text(new_text, encoding="utf-8")
    return True


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    engine_cpp = root / "src" / "engine.cpp"
    out_dir = root / args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    current_default = read_current_default(engine_cpp)
    candidates = [int(x.strip()) for x in args.candidates.split(",") if x.strip()]
    if args.base not in candidates:
        candidates.append(args.base)
    candidates = sorted(set(candidates))

    print(f"[INFO] current default={current_default}, base={args.base}, candidates={candidates}")

    results: list[MatchResult] = []
    for c in candidates:
        if c == args.base:
            continue
        results.append(run_match(args, c))

    if not results:
        print("[INFO] No candidate run was requested.")
        return 0

    best = sorted(results, key=lambda r: (r.elo, r.score, -r.draw_ratio), reverse=True)[0]
    print(f"[BEST] candidate={best.candidate}, elo={best.elo:+.2f}, score={best.score:.3f}")

    changed = False
    if best.candidate != current_default and best.elo > 0:
        changed = write_new_default(engine_cpp, best.candidate)
        if changed:
            print(f"[UPDATE] src/engine.cpp: Lc0Policy_Threads {current_default} -> {best.candidate}")
    else:
        print("[UPDATE] Keep current default (no positive improvement over baseline).")

    payload = {
        "timestamp": datetime.utcnow().isoformat() + "Z",
        "current_default": current_default,
        "base": args.base,
        "candidates": candidates,
        "best": asdict(best),
        "results": [asdict(r) for r in results],
        "changed": changed,
    }
    out_file = out_dir / f"lc0_threads_tune_{datetime.utcnow().strftime('%Y%m%d_%H%M%S')}.json"
    out_file.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"[OUT] {out_file}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

