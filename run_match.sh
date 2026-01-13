#!/bin/bash

# --- Dependencies ---
# This script requires 'cutechess-cli' and 'gawk' (or 'awk').
# It also requires a C++ compiler (like g++) and 'make' to build the engine.
#
# On Debian/Ubuntu: sudo apt-get install cutechess-cli gawk build-essential
# On Fedora: sudo dnf install cutechess-cli gawk make gcc-c++
# On macOS: brew install cutechess gawk

# --- Configuration ---
# Path for the NEW engine we want to test
NEW_ENGINE="./stockfish_new"

# Path for the BASELINE engine for comparison
BASELINE_ENGINE="./stockfish_baseline"

# Number of games to play
GAMES=20

# Time control (e.g., "1+0.1" for 1 second + 0.1s increment)
TC="1+0.1"

# Opening book
BOOK_FILE="opening_book.pgn"

# --- Script Logic ---

# Function to compile the engine
compile_engine() {
    local source_dir="src"
    local output_name=$1
    echo "Compiling engine in '$source_dir'..."

    make -C $source_dir clean > /dev/null 2>&1
    make -C $source_dir build ARCH=x86-64-modern -j$(nproc)

    if [ $? -ne 0 ]; then
        echo "ERROR: Compilation failed!"
        return 1
    fi

    mv "$source_dir/stockfish" "$output_name"
    echo "Compilation successful. Binary is at '$output_name'."
    return 0
}

# --- Main Execution ---

# 1. Check for baseline engine. If it doesn't exist, create it.
if [ ! -f "$BASELINE_ENGINE" ]; then
    echo "--- Baseline engine not found. Creating one now... ---"
    compile_engine "$BASELINE_ENGINE"
    if [ $? -ne 0 ]; then
        echo "Failed to compile baseline engine. Aborting."
        exit 1
    fi
    echo
    echo "Baseline '$BASELINE_ENGINE' created successfully."
    echo "Now, make your desired code changes in the 'src/' directory."
    echo "Then, run this script again to test your changes against the baseline."
    exit 0
fi

# 2. If the baseline exists, compile the new version of the engine.
echo "--- Compiling New Engine (with your changes) ---"
compile_engine "$NEW_ENGINE"
if [ $? -ne 0 ]; then
    echo "Failed to compile new engine. Aborting."
    exit 1
fi

# 3. Create a small dummy opening book if it doesn't exist.
if [ ! -f "$BOOK_FILE" ]; then
    echo "--- Creating a dummy opening book ($BOOK_FILE) ---"
    cat << EOB > $BOOK_FILE
[Event "?"]
[Site "?"]
[Date toupper]
[Round "?"]
[White "?"]
[Black "?"]
[Result "*"]
*
1. e4 e5 *
1. d4 d5 *
EOB
fi

# 4. Check for cutechess-cli dependency.
if ! command -v cutechess-cli &> /dev/null; then
    echo
    echo "ERROR: cutechess-cli is not installed or not in your PATH."
    echo "Please install it to run a match. See dependency notes at the top of this script."
    exit 1
fi

# 5. Run the match.
echo "--- Starting Match: $NEW_ENGINE vs $BASELINE_ENGINE ---"
NEW_ENGINE_NAME=$(basename $NEW_ENGINE)
BASELINE_ENGINE_NAME=$(basename $BASELINE_ENGINE)

cutechess-cli \
    -engine cmd=$NEW_ENGINE name=$NEW_ENGINE_NAME \
    -engine cmd=$BASELINE_ENGINE name=$BASELINE_ENGINE_NAME \
    -each tc=$TC \
    -games $GAMES \
    -openings file=$BOOK_FILE format=pgn \
    -concurrency $(nproc) \
    -pgnout match_results.pgn

echo "--- Match Finished ---"
echo "Results saved to match_results.pgn"

# 6. Parse results correctly using gawk.
echo "--- Results Summary ---"
SCORE=$(gawk -v new_engine="$NEW_ENGINE_NAME" '
    BEGIN {
        wins=0; losses=0; draws=0;
        # Compile the regex strings for matching, escaping special characters
        gsub(/\+/, "\\+", new_engine);
        white_re = "\\[White \"" new_engine "\"\\]";
        black_re = "\\[Black \"" new_engine "\"\\]";
    }
    # For each line, check for player and result tags
    $0 ~ white_re { player = "white"; }
    $0 ~ black_re { player = "black"; }

    /\[Result "1-0"\]/ {
        if (player == "white") wins++;
        else losses++;
        player = ""; # Reset player for the next game block
    }
    /\[Result "0-1"\]/ {
        if (player == "black") wins++;
        else losses++;
        player = ""; # Reset player
    }
    /\[Result "1\/2-1\/2"\]/ {
        draws++;
        player = ""; # Reset player
    }
    END {
        printf "+%d -%d =%d", wins, losses, draws;
    }
' match_results.pgn)

echo "Score for $NEW_ENGINE_NAME vs $BASELINE_ENGINE_NAME: $SCORE"
