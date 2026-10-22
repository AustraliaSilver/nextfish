"""Pure helpers for the HARENN data pipeline.

The functions here have no third party dependencies on purpose, so that they
can be imported and unit tested without numpy, torch or python-chess being
installed.
"""

from typing import Optional


def fen_side_to_move(fen: str) -> int:
    """0 = white to move, 1 = black to move."""
    parts = fen.split()
    if len(parts) < 2:
        raise ValueError(f"Malformed FEN: {fen!r}")
    return 0 if parts[1] == 'w' else 1


def fen_move_number(fen: str) -> Optional[int]:
    """The full move number of a FEN, or None when the FEN does not carry one."""
    parts = fen.split()
    if len(parts) < 6:
        return None
    try:
        return int(parts[5])
    except ValueError:
        return None


def game_id_from_name(stem: str) -> str:
    """Derive a game identifier from a data file name (w0_g7_b3_... -> g7)."""
    parts = stem.split('_')
    for part in parts:
        if part.startswith('g') and part[1:].isdigit():
            return f"g{part[1:]}"
    return stem
