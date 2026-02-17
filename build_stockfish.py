#!/usr/bin/env python3
"""
Build Stockfish Original with same optimizations as Nextfish
For fair comparison testing
"""

import subprocess
import sys
import os
import shutil
from pathlib import Path
from datetime import datetime
import json

from build_optimizations import (
    common_cxxflags,
    pgo_gen_flags,
    pgo_use_flags,
    merge_flags,
    default_pgo_profile_dir,
)

class StockfishBuilder:
    def __init__(self):
        self.base_dir = Path(os.environ.get("CAI_ROOT", str(Path(__file__).resolve().parent))).resolve()
        self.src_dir = self.base_dir / "Stockfish-master" / "Stockfish-master" / "src"
        self.build_dir = self.base_dir / "builds"
        
        self.build_dir.mkdir(exist_ok=True)
        
        self.timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.stockfish_exe = self.build_dir / f"stockfish_orig_{self.timestamp}.exe"
        
        # Same compiler settings as Nextfish
        self.cxx = "g++"
        self.pgo_phase = os.environ.get("PGO_PHASE", "").strip().lower()
        self.pgo_dir = None
        self.pgo_error = None

        flags = common_cxxflags(
            sse_flag="4.1",
            enable_lto=True,
            enable_unroll=True,
            omit_frame_pointer=True,
            static_link=True,
        )

        if self.pgo_phase == "gen":
            self.pgo_dir = default_pgo_profile_dir(self.build_dir, "stockfish", self.timestamp)
            self.pgo_dir.mkdir(parents=True, exist_ok=True)
            flags = merge_flags(flags, pgo_gen_flags(self.pgo_dir))
        elif self.pgo_phase == "use":
            pgo_dir = os.environ.get("PGO_DIR", "").strip()
            if not pgo_dir:
                self.pgo_error = "PGO_PHASE=use requires PGO_DIR pointing to an existing profile directory"
            else:
                self.pgo_dir = Path(pgo_dir)
                flags = merge_flags(flags, pgo_use_flags(self.pgo_dir))

        self.cxxflags = flags
        self.build_timeout_seconds = 10 * 60
        
    def log(self, message):
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {message}")
        
    def get_source_files(self):
        """Get list of source files to compile"""
        sources = [
            "benchmark.cpp",
            "bitboard.cpp",
            "evaluate.cpp",
            "main.cpp",
            "misc.cpp",
            "movegen.cpp",
            "movepick.cpp",
            "position.cpp",
            "search.cpp",
            "thread.cpp",
            "timeman.cpp",
            "tt.cpp",
            "uci.cpp",
            "ucioption.cpp",
            "tune.cpp",
            "engine.cpp",
            "score.cpp",
            "memory.cpp"
        ]
        
        # Add syzygy
        syzygy_sources = [
            "syzygy/tbprobe.cpp"
        ]
        
        # Add NNUE
        nnue_sources = [
            "nnue/nnue_accumulator.cpp",
            "nnue/nnue_misc.cpp",
            "nnue/network.cpp",
            "nnue/features/half_ka_v2_hm.cpp",
            "nnue/features/full_threats.cpp"
        ]
        
        all_sources = sources + syzygy_sources + nnue_sources
        return [str(self.src_dir / f) for f in all_sources]
        
    def check_compiler(self):
        """Check if g++ is available"""
        try:
            result = subprocess.run(
                [self.cxx, "--version"],
                capture_output=True,
                text=True,
                shell=True
            )
            if result.returncode == 0:
                self.log(f"Compiler found: {result.stdout.split(chr(10))[0]}")
                return True
            return False
        except Exception as e:
            self.log(f"Compiler check failed: {e}")
            return False
            
    def compile(self):
        """Compile Stockfish directly"""
        self.log("Starting Stockfish compilation...")

        if self.pgo_error:
            self.log(f"ERROR: {self.pgo_error}")
            return False

        if not self.check_compiler():
            self.log("ERROR: g++ not found!")
            return False
            
        sources = self.get_source_files()
        
        # Verify all source files exist
        for src in sources:
            if not os.path.exists(src):
                self.log(f"ERROR: Source file not found: {src}")
                return False
                
        if self.pgo_phase:
            self.log(f"PGO phase: {self.pgo_phase} (dir: {self.pgo_dir})")
        self.log(f"Compiling {len(sources)} source files...")
        
        # Compile command
        cmd = [self.cxx] + self.cxxflags + sources + ["-o", str(self.stockfish_exe)]
        
        self.log(f"Command: {' '.join(cmd[:10])} ... ({len(cmd)} args total)")
        
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                shell=True,
                cwd=str(self.src_dir),
                timeout=self.build_timeout_seconds
            )
            
            if result.returncode != 0:
                self.log(f"COMPILATION FAILED!")
                self.log(f"STDERR: {result.stderr[-3000:] if len(result.stderr) > 3000 else result.stderr}")
                return False
                
            if self.stockfish_exe.exists():
                self.log(f"[OK] Stockfish build successful: {self.stockfish_exe}")
                return True
            else:
                self.log("ERROR: Executable not created")
                return False
                
        except subprocess.TimeoutExpired:
            self.log(f"COMPILATION TIMEOUT after {self.build_timeout_seconds} seconds")
            return False
        except Exception as e:
            self.log(f"Compilation error: {e}")
            return False

def main():
    print("="*60)
    print("Stockfish Original Build System")
    print("="*60)
    
    builder = StockfishBuilder()
    
    if not builder.compile():
        print("\n[FAIL] Build failed!")
        sys.exit(1)
    print("\n[OK] Stockfish build completed!")
    print(f"Executable: {builder.stockfish_exe}")

if __name__ == "__main__":
    main()

