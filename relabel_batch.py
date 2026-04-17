import json
import chess
import sys
import os
from multiprocessing import Pool
from tqdm import tqdm
from pathlib import Path

from analyzer import PositionAnalyzer

def process_line(line):
    try:
        data = json.loads(line.strip())
        fen = data.get("fen")
        if not fen:
            return None
        
        board = chess.Board(fen)
        
        # Calculate new labels using the improved logic
        data["tau"] = round(PositionAnalyzer.calculate_tau(board), 4)
        data["rho"] = round(PositionAnalyzer.calculate_rho(board), 4)
        data["rs"] = round(PositionAnalyzer.calculate_rs(board), 4)
        
        return json.dumps(data)
    except Exception as e:
        return None

def process_file(file_path):
    output_path = file_path.with_suffix(".jsonl_labeled")
    print(f"Processing {file_path.name}...")
    
    with open(file_path, "r") as f:
        lines = f.readlines()
    
    with Pool(processes=os.cpu_count()) as pool:
        results = list(tqdm(pool.imap(process_line, lines), total=len(lines), desc=file_path.name))
    
    with open(output_path, "w") as f:
        for result in results:
            if result:
                f.write(result + "\n")
    
    print(f"Finished {file_path.name}. Saved to {output_path.name}")
    # Replace original with labeled version
    os.remove(file_path)
    os.rename(output_path, file_path)

def main():
    files_to_label = [
        "harenn_standard.jsonl",
        "harenn_standard_24454078863.jsonl",
        "harenn_standard_24464726721.jsonl",
        "harenn_standard_24477292483.jsonl",
        "harenn_standard_24488009496.jsonl",
        "harenn_standard_24490813671.jsonl",
        "harenn_standard_24496520626.jsonl"
    ]
    
    base_dir = Path(r"D:\nextfish\downloaded_data")
    
    for filename in files_to_label:
        file_path = base_dir / filename
        if file_path.exists():
            process_file(file_path)
        else:
            print(f"File not found: {filename}")

if __name__ == "__main__":
    main()
