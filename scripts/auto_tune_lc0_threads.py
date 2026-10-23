#!/usr/bin/env python3
"""
Auto-tune a UCI option default in src/engine.cpp using fastchess self-play.

This script compares candidate values against a baseline value using the same
engine binary, then updates the default UCI option value if a better candidate
is found.
"""


import argparse
import json
import math
import os
import re
import subprocess
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


DEFAULT_OPTION = "Lc0Policy_Threads"

# Pre-compiled regular expressions for match output parsing
SCORE_RE = re.compile(r"Score of .*?:\s*(\d+)\s*-\s*(\d+)\s*-\s*(\d+)\s*\[([0-9.]+)%\]\s*(\d+)")
METRICS_RE = re.compile(
    r"Elo difference:\s*([\-+0-9.infna]+)\s*\+/-\s*([0-9.infna]+),\s*LOS:\s*([0-9.]+)\s*%,\s*DrawRatio:\s*([0-9.]+)\s*%"
)


class MatchTimeout(RuntimeError):
    pass


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
    engine_name = "stockfish.exe" if os.name == "nt" else "stockfish"
    parser = argparse.ArgumentParser(description="Tune a UCI option default via fastchess.")
    parser.add_argument("--root", default=str(root))
    parser.add_argument("--engine", default=str(root / "src" / engine_name))
    parser.add_argument("--fastchess", default="./fastchess")
    parser.add_argument("--book", default="UHO_2022_8mvs_+110_+119.pgn")
    parser.add_argument("--tc", default="10+0.1")
    parser.add_argument("--rounds", type=int, default=8, help="Fastchess rounds (2 games each).")
    parser.add_argument("--concurrency", type=int, default=2)
    parser.add_argument("--option", default=DEFAULT_OPTION, help="UCI option to tune.")
    parser.add_argument("--base", type=int, default=2, help="Baseline value of the option.")
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
    score_all = list(SCORE_RE.finditer(output))
    if not score_all:
        raise RuntimeError("Could not parse fastchess score line.")
    w, l, d, s, g = score_all[-1].groups()
    # the fastchess score is a percentage, score_to_elo expects a fraction
    score = float(s) / 100.0

    elo_all = list(METRICS_RE.finditer(output))
    if elo_all:
        e, ci, los, dr = elo_all[-1].groups()
        elo = float(e.replace("+", "")) if e not in ("inf", "+inf", "-inf", "nan") else score_to_elo(score)
        elo_ci = float(ci) if ci not in ("nan", "inf", "+inf", "-inf") else float("nan")
        los_v = float(los)
        dr_v = float(dr)
    else:
        elo = score_to_elo(score)
        elo_ci = float("nan")
        los_v = float("nan")
        dr_v = (100.0 * int(d)) / max(1, int(g))

    return MatchResult(
        candidate=candidate,
        wins=int(w),
        losses=int(l),
        draws=int(d),
        score=score,
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
        f"option.{args.option}={candidate}",
        "-engine",
        f"cmd={args.engine}",
        "name=Baseline",
        f"option.{args.option}={args.base}",
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
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        raise MatchTimeout(
            f"fastchess timed out after {args.timeout} seconds for candidate={candidate}"
        )
    output = (proc.stdout or "") + (proc.stderr or "")
    if "Score of" not in output:
        raise RuntimeError(f"fastchess did not return score for candidate={candidate}")
    result = parse_match_output(output, candidate)
    print(
        f"[RES] candidate={candidate} WLD={result.wins}-{result.losses}-{result.draws} "
        f"score={result.score:.3f} elo={result.elo:+.2f} los={result.los:.1f}%"
    )
    return result


def option_pattern(option_name: str) -> re.Pattern:
    return re.compile(
        r'options\.add\("' + re.escape(option_name) + r'",\s*Option\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)'
    )


def validate_option(engine_cpp: Path, option_name: str) -> None:
    text = engine_cpp.read_text(encoding="utf-8")
    if f'options.add("{option_name}"' not in text:
        raise RuntimeError(
            f'UCI option "{option_name}" does not exist in {engine_cpp}. '
            f"Pass --option with an option that the engine defines."
        )


def read_current_default(engine_cpp: Path, option_name: str) -> int:
    text = engine_cpp.read_text(encoding="utf-8")
    m = option_pattern(option_name).search(text)
    if not m:
        raise RuntimeError(f"Cannot find {option_name} default in {engine_cpp}")
    return int(m.group(1))


def write_new_default(engine_cpp: Path, option_name: str, new_value: int) -> bool:
    text = engine_cpp.read_text(encoding="utf-8")
    new_text, count = option_pattern(option_name).subn(
        lambda m: f'options.add("{option_name}", Option({new_value}, {m.group(2)}, {m.group(3)}',
        text,
        count=1,
    )
    if count != 1:
        raise RuntimeError(f"Failed to update {option_name} default in {engine_cpp}")
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

    try:
        validate_option(engine_cpp, args.option)
        current_default = read_current_default(engine_cpp, args.option)
    except RuntimeError as e:
        print(f"[ERROR] {e}")
        return 1
    candidates = [int(x.strip()) for x in args.candidates.split(",") if x.strip()]
    if args.base not in candidates:
        candidates.append(args.base)
    candidates = sorted(set(candidates))

    print(f"[INFO] option={args.option}, current default={current_default}, "
          f"base={args.base}, candidates={candidates}")

    results: list[MatchResult] = []
    errors: list[str] = []
    for c in candidates:
        if c == args.base:
            continue
        try:
            results.append(run_match(args, c))
        except MatchTimeout as e:
            print(f"[ERROR] {e}")
            errors.append(str(e))
        except Exception as e:
            print(f"[ERROR] candidate={c} failed: {e}")
            errors.append(str(e))

    changed = False
    best = None
    if results:
        best = sorted(results, key=lambda r: (r.elo, r.score, -r.draw_ratio), reverse=True)[0]
        print(f"[BEST] candidate={best.candidate}, elo={best.elo:+.2f}, score={best.score:.3f}, "
              f"LOS={best.los:.1f}%")

        if best.candidate != current_default and best.elo > 0 and best.los >= 95.0:
            changed = write_new_default(engine_cpp, args.option, best.candidate)
            if changed:
                print(f"[UPDATE] src/engine.cpp: {args.option} {current_default} -> {best.candidate}")
        else:
            print("[UPDATE] Keep current default (no significant improvement over baseline).")
    else:
        print("[INFO] No candidate run produced a result.")

    now = datetime.now(timezone.utc)
    payload = {
        "timestamp": now.isoformat(),
        "option": args.option,
        "current_default": current_default,
        "base": args.base,
        "candidates": candidates,
        "best": asdict(best) if best else None,
        "results": [asdict(r) for r in results],
        "errors": errors,
        "changed": changed,
    }
    out_file = out_dir / f"{args.option.lower()}_tune_{now.strftime('%Y%m%d_%H%M%S')}.json"
    out_file.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"[OUT] {out_file}")
    return 0 if results else 1


if __name__ == "__main__":
    raise SystemExit(main())
