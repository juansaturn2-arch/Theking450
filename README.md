# Theking450
TheKing450 is a high-performance chess engine built on a modified CFish / Stockfish codebase. It combines deep alpha-beta search, modern evaluation, and aggressive tactical precision, delivering brutal calculation strength while retaining the classic Chessmaster “The King” playing identity.

Not Actual Codebase of Chessmaster but the Personality Credits to Original Creator to Johan de Koning for inspiring me to
Create this Chess Engine

# TheKing450 - WinBoard 2 Protocol Chess Engine

## Overview
TheKing450 is a modified version of CLSD (Cfish/Stockfish in C) converted to WinBoard/XBoard Protocol v2 with custom PV line formatting (depth×1001) designed for Chessmaster compatibility.

## Features

### Core Features (from CLSD/Cfish)
- **Full Stockfish search algorithm** - Alpha-beta, null move pruning, LMR, etc.
- **Classical evaluation** - Material, PST, pawns, mobility, king safety
- **Transposition table** - Configurable hash size
- **Multi-threading support** - Multiple CPU cores
- **Time management** - Tournament time controls

### WinBoard v2 Protocol
- Full WB2 protocol compliance for GUI integration
- Works with Arena, WinBoard, Chessmaster, etc.

### Custom PV Lines (Chessmaster Compatible)
PV lines output with depth multiplied by 1001:
```
1001 109 0 20 e2e3
2002 121 0 47 e2e3 b7b6
3003 171 0 100 e2e4 d7d6 d2d4
...
12012 86 6 84700 e2e4 e7e5 g1f3 b8c6...
move e2e4
```

## Compilation

### Linux (GCC)
```bash
gcc -O3 -march=native -DNDEBUG -DIS_64BIT -DUSE_POPCNT TheKing450.c -o TheKing450 -lpthread -lm
```

### Windows (MinGW/MSYS2 - UCRT64)
```bash
clang -O3 -march=native TheKing450.c -o TheKing450.exe -fuse-ld=lld
```

## Estimated Strength
~3500+ ELO (same as CLSD/Cfish classical evaluation)

## Credits
- Based on CLSD/CLBrain by K7 ChessLab
- Original Cfish by Syzygy (Stockfish port to C)
- Original Stockfish by the Stockfish developers
- WB2 protocol conversion by K7 ChessLab 2026
