"""
FEN parsing helper functions for HARENN training.
"""

def fen_side_to_move(fen: str) -> int:
    """Return 0 for White ('w') and 1 for Black ('b')."""
    parts = fen.strip().split()
    if len(parts) > 1 and parts[1].lower() == 'b':
        return 1
    return 0

def fen_move_number(fen: str) -> int:
    """Return the fullmove number from the FEN string (default 1)."""
    parts = fen.strip().split()
    if len(parts) >= 6:
        try:
            return int(parts[5])
        except (ValueError, TypeError):
            pass
    return 1
