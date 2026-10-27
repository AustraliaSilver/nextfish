import json
import chess
import os
import argparse
from multiprocessing import Pool
from tqdm import tqdm
from pathlib import Path

from analyzer import PositionAnalyzer

REPO_ROOT = Path(__file__).resolve().parent
DEFAULT_DATA_DIR = REPO_ROOT / "data"

FILES_TO_LABEL = [
    "harenn_standard.jsonl",
    "harenn_standard_24454078863.jsonl",
    "harenn_standard_24464726721.jsonl",
    "harenn_standard_24477292483.jsonl",
    "harenn_standard_24488009496.jsonl",
    "harenn_standard_24490813671.jsonl",
    "harenn_standard_24496520626.jsonl",
]


def process_line(line):
    try:
        data = json.loads(line.strip())
    except json.JSONDecodeError as e:
        return None, f"invalid JSON: {e}"

    if not isinstance(data, dict):
        return None, "record is not a JSON object"

    fen = data.get("fen")
    if not fen:
        return None, "missing 'fen' field"

    try:
        board = chess.Board(fen)

        # Calculate new labels using the improved logic
        data["tau"] = round(PositionAnalyzer.calculate_tau(board), 4)
        data["rho"] = round(PositionAnalyzer.calculate_rho(board), 4)
        data["rs"] = round(PositionAnalyzer.calculate_rs(board), 4)
    except Exception as e:
        return None, f"cannot analyse FEN {fen!r}: {e}"

    return json.dumps(data), None


def process_file(file_path, pool):
    output_path = file_path.with_name(file_path.name + ".labeled")
    print(f"Processing {file_path.name}...")

    with open(file_path, "r") as f:
        lines = f.readlines()

    results = list(tqdm(pool.imap(process_line, lines), total=len(lines), desc=file_path.name))

    written = 0
    skipped = 0
    reasons = []
    with open(output_path, "w") as f:
        for result, reason in results:
            if result is not None:
                f.write(result + "\n")
                written += 1
            else:
                skipped += 1
                if len(reasons) < 5:
                    reasons.append(reason)

    print(f"Finished {file_path.name}: {written} written, {skipped} skipped")
    for reason in reasons:
        print(f"  skipped: {reason}")
    if skipped > len(reasons):
        print(f"  ... and {skipped - len(reasons)} more skipped lines")

    # Replace original with labeled version (atomic, temp file kept on failure)
    os.replace(output_path, file_path)
    return written, skipped


def collect_files(base_dir):
    listed = [base_dir / name for name in FILES_TO_LABEL if (base_dir / name).exists()]
    known = {path.name for path in listed}
    for path in sorted(base_dir.glob("*.jsonl")):
        if path.name not in known:
            listed.append(path)
    return listed


def main():
    parser = argparse.ArgumentParser(description="Relabel HARENN jsonl corpora")
    parser.add_argument("--data-dir", default=str(DEFAULT_DATA_DIR),
                        help="Directory containing the jsonl files")
    args = parser.parse_args()

    base_dir = Path(args.data_dir)
    if not base_dir.exists():
        print(f"Data directory not found: {base_dir}")
        return

    files = collect_files(base_dir)
    if not files:
        print(f"No jsonl files found in {base_dir}")
        return

    total_written = 0
    total_skipped = 0
    with Pool(processes=os.cpu_count()) as pool:
        for file_path in files:
            written, skipped = process_file(file_path, pool)
            total_written += written
            total_skipped += skipped

    print(f"\nAll done: {total_written} positions written, {total_skipped} lines skipped")


if __name__ == "__main__":
    main()
