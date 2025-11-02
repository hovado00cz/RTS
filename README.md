# Update: A* pathfinding + Worker gathering + Sprites + HUD

What's new:
- A* pathfinding (diagonals enabled), per-tick path consumption.
- Worker RMB on **G** (gold) or **T** (trees) -> auto **gather → deliver** loop.
- Drop-off tile **D** (a hut). Delivery increases **Gold/Wood**.
- Sprite rendering via **BMP atlases** with magenta color key (no solid squares).
- Simple HUD bar with Gold/Wood counters (bitmap digits, no SDL_ttf required).

Controls:
- LMB drag to select, RMB on ground to move, RMB on resources with worker to gather.
