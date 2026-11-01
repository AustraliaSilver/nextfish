#pragma once

#include "types.h"
#include "position.h"

namespace NextFish {

struct MoveList {
    Move moves[256];
    int count = 0;

    void push_back(Move m) { moves[count++] = m; }
    int size() const { return count; }
    bool empty() const { return count == 0; }
    void clear() { count = 0; }

    Move operator[](int i) const { return moves[i]; }
    Move& operator[](int i) { return moves[i]; }

    Move* begin() { return moves; }
    Move* end() { return moves + count; }
    const Move* begin() const { return moves; }
    const Move* end() const { return moves + count; }
};

enum GenType {
    CAPTURES,
    QUIETS,
    EVASIONS,
    ALL
};

template <GenType Type>
void generate_moves(const Position& pos, MoveList& list);

inline void generate_all_legal(const Position& pos, MoveList& list) {
    MoveList pseudo;
    if (pos.is_in_check()) {
        generate_moves<EVASIONS>(pos, pseudo);
    } else {
        generate_moves<ALL>(pos, pseudo);
    }
    list.clear();
    for (int i = 0; i < pseudo.size(); ++i) {
        if (pos.is_legal(pseudo[i])) {
            list.push_back(pseudo[i]);
        }
    }
}

} // namespace NextFish
