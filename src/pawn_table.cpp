#include "pawn_table.h"

namespace NextFish {

PawnTable GlobalPawnTable;

PawnTable::PawnTable() {
    clear();
}

void PawnTable::clear() {
    std::memset(entries, 0, sizeof(entries));
}

} // namespace NextFish
