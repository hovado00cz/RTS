
#pragma once
#include <vector>
struct Map; struct Vec2i;
bool astar_find(const Map& map, Vec2i start, Vec2i goal, std::vector<Vec2i>& out_path, bool diag=false, int max_nodes=8192);
