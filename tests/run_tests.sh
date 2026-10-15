#!/usr/bin/env bash
# Run the game-rule tests on the build host.
#
# The rules live in VOBGTetris.c with no Foundation or UIKit dependency, so the
# whole thing can be compiled and exercised here on Linux -- no Mac, no
# simulator, no device needed. This is the only automated check the project has,
# so run it before and after touching VOBGTetris.c.
set -euo pipefail

cd "$(dirname "$0")"
CC="${CC:-clang}"
OUT="build/test_tetris"

mkdir -p build
"$CC" -std=c99 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined \
    -I.. \
    test_tetris.c ../VOBGTetris.c -o "$OUT" -lm

"$CC" -std=c99 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined \
    -I.. \
    test_gesture.c ../VOBGGesture.c -o build/test_gesture -lm

"$CC" -std=c99 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined \
    -I.. \
    test_play.c ../VOBGTetris.c ../VOBGGesture.c -o build/test_play -lm

"$CC" -std=c99 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined \
    -I.. \
    test_layout.c ../VOBGTetris.c -o build/test_layout -lm

"$CC" -std=c99 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined \
    -I.. \
    test_drag.c ../VOBGTetris.c -o build/test_drag -lm

"$CC" -std=c99 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined \
    -I.. \
    test_layout_order.c ../VOBGTetris.c -o build/test_layout_order -lm

"$OUT"
printf '\n'
build/test_gesture
printf '\n'
build/test_play
printf '\n'
build/test_layout
printf '\n'
build/test_drag
printf '\n'
exec build/test_layout_order