
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <deque>
#include "types.hpp"

struct Sim;

struct Vec2i{ int x,y; };

enum class ResourceKind : uint8_t { None=0, Gold=2, Wood=3 };

struct Map {
    int width=0, height=0;
    std::vector<uint8_t> tiles;   // už máš
    std::vector<uint8_t> blocked; // už máš

    // NOVÉ:
    std::vector<uint8_t> res_kind;   // 0=None, 2=Gold, 3=Wood  (kopíruje semantiku tiles)
    std::vector<int>     res_amount; // zůstatek (např. Wood=300, Gold=500)
    std::vector<int>     res_max;
};

enum class UnitJob:uint8_t{ Idle, Moving, GatheringGold, GatheringWood, Delivering, Building };

struct Unit{
    UnitId id; uint16_t type_index;
    Vec2i tile; Vec2i goal;
    std::vector<Vec2i> path;
    int hp; uint16_t cooldown;
    bool selected=false;
    UnitJob job=UnitJob::Idle;
    int carried=0; // carried amount
    uint8_t carried_kind=0; // 0 none, 1 gold, 2 wood
    BuildingId building_target=0; // for building
};

struct TrainItem{ uint16_t unit_type; int remaining_ms; };

struct Building{
    BuildingId id; BuildingKind kind; Vec2i tile; // top-left of footprint
    int w=1,h=1;
    BuildState state=BuildState::Planned;
    int build_progress_ms=0; int build_total_ms=1000;
    int cost_gold=0, cost_wood=0;
    std::deque<TrainItem> queue; // production queue (barracks)
    bool selected=false;
    Vec2i rally{ -1, -1 }; 
};

struct SimConfig{ uint32_t seed=12345; int tick_rate=RTS_FIXED_TICK; };

struct Sim{
    SimConfig cfg;
    Map map;
    std::vector<UnitType> unit_types;
    std::unordered_map<std::string,uint16_t> unit_type_index;
    std::vector<Unit> units; uint32_t next_unit_id=1;
    std::vector<Building> buildings; uint32_t next_building_id=1;
    int gold=500, wood=0, food_used=0, food_cap=10;
    std::vector<Vec2i> dropoffs; // all drop-off tiles (centers / 1x1)
    Sim();
};

bool load_data(Sim& s, const std::string& assets_path);
void step(Sim& s, uint32_t dt_ms);

UnitId spawn_unit(Sim& s, const std::string& unit_id, int x, int y);
void order_move(Sim& s, UnitId u, int gx, int gy);
void order_gather(Sim& s, UnitId u, int tx, int ty);

struct BuildingType{
    BuildingKind kind; const char* id;
    int w, h;
    int cost_gold, cost_wood;
    int build_ms;
    int food_cap_bonus;
};

bool can_place_building(const Sim& s, BuildingType bt, int x, int y);
BuildingId start_building(Sim& s, BuildingType bt, int x, int y); // deducts resources, reserves tiles
void cancel_building(Sim& s, BuildingId id, bool refund);
void remove_building(Sim& s, BuildingId id, bool refund);
Vec2i nearest_dropoff(const Sim& s, Vec2i from);
BuildingType get_btype(BuildingKind k);
Building* find_building(Sim& s, BuildingId id);
const Building* find_building(const Sim& s, BuildingId id);

// Production
bool queue_train(Sim& s, Building& b, const std::string& unit_id, int count);
bool cancel_last_train(Sim& s, Building& b);
bool cancel_train_at(Sim& s, Building& b, size_t idx);

void init_resources_from_tiles(Sim& sim, int wood_amount=300, int gold_amount=500);
bool resource_take_at(Sim& s, int x, int y, int amount, int* actually_taken);
static inline int ridx(const Map& m, int x, int y){ return y*m.width + x; }

// --- Save/Load API ---
bool save_game(const Sim& s, const std::string& path);
bool load_game(Sim& s, const std::string& path);