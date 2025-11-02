
#pragma once
#include <cstdint>
#include <vector>
#include <string>
using UnitId = uint32_t;
using BuildingId = uint32_t;

enum class ArmorType : uint8_t { Light, Medium, Heavy, Building };
enum class AttackType : uint8_t { Normal, Pierce, Siege, Magic };
struct Attack{ AttackType type; int16_t damage; uint16_t cooldown_ms; uint16_t range_tiles; };
struct Armor{ ArmorType type; int16_t value; };
struct UnitType{
    std::string id; int hp; int move_speed_px_s;
    Attack attack; Armor armor; int sight_tiles;
    int cost_gold, cost_wood, food; int build_time_ms;
};

enum class BuildingKind : uint8_t { Dropoff=0, Farm=1, Barracks=2 };
enum class BuildState : uint8_t { Planned=0, Constructing=1, Complete=2, Canceled=3 };
