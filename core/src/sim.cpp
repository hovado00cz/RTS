
#include "sim.hpp"
#include "pathfinding.hpp"
#include "data.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <sstream>

Sim::Sim(){}

static inline int idx(int w,int x,int y){ return y*w+x; }
static inline bool in_bounds(const Map& m,int x,int y){ return !(x<0||y<0||x>=m.width||y>=m.height); }
static inline bool walkable(const Map& m,int x,int y){
    if(!in_bounds(m,x,y)) return false;
    if(m.blocked[idx(m.width,x,y)]) return false;
    uint8_t t=m.tiles[idx(m.width,x,y)];
    return t!=1; // walls only
}

static inline bool is_resource_tile(const Map& m, int x, int y, ResourceKind kind){
    if(x<0||y<0||x>=m.width||y>=m.height) return false;
    int i = idx(m.width,x,y);
    return m.res_kind[i] == (uint8_t)kind && m.res_amount[i] > 0;
}

// Najdi vedle pozice (x,y) sousední dlaždici se surovinou 'kind' (8-směrně)
static bool find_adjacent_resource_at(const Sim& s, int x, int y, ResourceKind kind, Vec2i* outRes){
    for(int dy=-1; dy<=1; ++dy) for(int dx=-1; dx<=1; ++dx){
        if(dx==0 && dy==0) continue;
        int nx=x+dx, ny=y+dy;
        if(is_resource_tile(s.map, nx, ny, kind)){ if(outRes) *outRes={nx,ny}; return true; }
    }
    return false;
}

// Najdi nejlepší "stání" vedle resource dlaždice (nejbližší k 'from'), které je průchozí
static bool find_stand_tile_for_resource(const Sim& s, Vec2i res, Vec2i from, Vec2i* outStand){
    Vec2i best = from; int bestd = 1e9; bool found=false;
    for(int dy=-1; dy<=1; ++dy) for(int dx=-1; dx<=1; ++dx){
        if(dx==0 && dy==0) continue;
        int sx=res.x+dx, sy=res.y+dy;
        if(!walkable(s.map, sx, sy)) continue;
        int d = std::abs(sx-from.x)+std::abs(sy-from.y);
        if(d<bestd){ bestd=d; best={sx,sy}; found=true; }
    }
    if(found && outStand) *outStand=best;
    return found;
}

// Najdi nejbližší (Manhattan) resource dlaždici s 'kind' a rovnou k ní spočítej stand-tile
static bool find_nearest_resource(const Sim& s, Vec2i from, ResourceKind kind, Vec2i* outRes, Vec2i* outStand){
    int best = std::numeric_limits<int>::max(); Vec2i bestRes; Vec2i bestStand; bool any=false;
    for(int y=0; y<s.map.height; ++y){
        for(int x=0; x<s.map.width; ++x){
            int i = idx(s.map.width,x,y);
            if(s.map.res_kind[i] != (uint8_t)kind || s.map.res_amount[i] <= 0) continue;
            // musí existovat aspoň jedno průchozí stání
            Vec2i stand;
            if(!find_stand_tile_for_resource(s, {x,y}, from, &stand)) continue;
            int d = std::abs(x-from.x)+std::abs(y-from.y);
            if(d < best){ best=d; bestRes={x,y}; bestStand=stand; any=true; }
        }
    }
    if(any){
        if(outRes) *outRes=bestRes;
        if(outStand) *outStand=bestStand;
    }
    return any;
}

UnitId spawn_unit(Sim& s, const std::string& unit_id, int x, int y){
    auto it=s.unit_type_index.find(unit_id);
    if(it==s.unit_type_index.end()) return 0;
    Unit u{}; u.id=s.next_unit_id++; u.type_index=it->second;
    u.tile={x,y}; u.goal={x,y}; u.hp=s.unit_types[u.type_index].hp; u.cooldown=0;
    s.units.push_back(u); return u.id;
}

void order_move(Sim& s, UnitId u, int gx, int gy){
    for(auto& e: s.units) if(e.id==u){
        e.goal={gx,gy}; e.job=UnitJob::Moving; e.path.clear();
        astar_find(s.map, e.tile, e.goal, e.path, true);
        break;
    }
}

void order_gather(Sim& s, UnitId u, int tx, int ty){
    for(auto& e: s.units) if(e.id==u){
        // Urči typ suroviny primárně z res_* polí
        ResourceKind kind = ResourceKind::None;
        if(tx>=0 && ty>=0 && tx<s.map.width && ty<s.map.height){
            int i = idx(s.map.width,tx,ty);
            if(s.map.res_kind[i] == (uint8_t)ResourceKind::Gold) kind = ResourceKind::Gold;
            else if(s.map.res_kind[i] == (uint8_t)ResourceKind::Wood) kind = ResourceKind::Wood;
            else {
                // fallback podle tile typu (2=zlato,3=les), kdyby res_* nebylo inicializované
                uint8_t tt = s.map.tiles[i];
                if(tt==2) kind = ResourceKind::Gold;
                else if(tt==3) kind = ResourceKind::Wood;
            }
        }
        if(kind==ResourceKind::None){ e.job=UnitJob::Idle; e.carried_kind=0; return; }

        // Nastav job + carried_kind
        e.job = (kind==ResourceKind::Gold) ? UnitJob::GatheringGold : UnitJob::GatheringWood;
        e.carried_kind = (kind==ResourceKind::Gold) ? 1 : 2;

        // Má dlaždice zásobu? když ne, najdi nejbližší
        Vec2i res = {tx,ty}, stand;
        bool hasResHere = is_resource_tile(s.map, tx, ty, kind);
        if(!hasResHere || !find_stand_tile_for_resource(s, res, e.tile, &stand)){
            // Hledej nejbližší zdroj stejného typu
            if(!find_nearest_resource(s, e.tile, kind, &res, &stand)){
                // žádný zdroj nezbyl
                e.job=UnitJob::Idle; return;
            }
        }

        // Vydej se na stand-tile vedle resource
        e.goal = stand; e.path.clear();
        astar_find(s.map, e.tile, e.goal, e.path, true);
        return;
    }
}


Vec2i nearest_dropoff(const Sim& s, Vec2i from){
    int best=std::numeric_limits<int>::max(); Vec2i ret = from;
    for(auto d: s.dropoffs){
        int dist = std::abs(d.x-from.x)+std::abs(d.y-from.y);
        if(dist<best){ best=dist; ret=d; }
    }
    return ret;
}

BuildingType get_btype(BuildingKind k){
    if(k==BuildingKind::Farm) return BuildingType{BuildingKind::Farm,"farm",1,1, 60, 40, 1200, 4};
    if(k==BuildingKind::Barracks) return BuildingType{BuildingKind::Barracks,"barracks",2,2, 150, 80, 5000, 0};
    return BuildingType{BuildingKind::Dropoff,"dropoff",1,1, 120, 0, 1500, 0};
}

static void set_block(Map& m, int x, int y, int w, int h, bool on) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            int nx = x + i, ny = y + j;
            if (nx >= 0 && ny >= 0 && nx < m.width && ny < m.height)
                m.blocked[ny * m.width + nx] = on ? 1 : 0;
        }
}

bool can_place_building(const Sim& s, BuildingType bt, int x, int y){
    for(int j=0;j<bt.h;++j) for(int i=0;i<bt.w;++i){
        int nx=x+i, ny=y+j;
        if(!in_bounds(s.map,nx,ny)) return false;
        if(s.map.blocked[idx(s.map.width,nx,ny)]) return false;
        uint8_t t = s.map.tiles[idx(s.map.width,nx,ny)];
        if(t!=0) return false;
    }
    return true;
}

BuildingId start_building(Sim& s, BuildingType bt, int x, int y) {
    if (s.gold < bt.cost_gold || s.wood < bt.cost_wood) return 0;
    if (!can_place_building(s, bt, x, y)) return 0;

    s.gold -= bt.cost_gold; s.wood -= bt.cost_wood;

    Building b{};
    b.id = s.next_building_id++;
    b.kind = bt.kind;
    b.tile = { x, y };
    b.w = bt.w; b.h = bt.h;
    b.state = BuildState::Planned;
    b.build_total_ms = bt.build_ms;
    b.cost_gold = bt.cost_gold;
    b.cost_wood = bt.cost_wood;

    s.buildings.push_back(b);
    set_block(s.map, x, y, bt.w, bt.h, /*on=*/true);
    return b.id;
}

void cancel_building(Sim& s, BuildingId id, bool refund) {
    for (size_t i = 0; i < s.buildings.size(); ++i) {
        if (s.buildings[i].id == id) {
            Building b = s.buildings[i];
            set_block(s.map, b.tile.x, b.tile.y, b.w, b.h, /*on=*/false);
            if (refund && b.state != BuildState::Complete) {
                s.gold += b.cost_gold;
                s.wood += b.cost_wood;
            }
            s.buildings.erase(s.buildings.begin() + i);
            return;
        }
    }
}

void remove_building(Sim& s, BuildingId id, bool refund){
    for(size_t i=0;i<s.buildings.size();++i){
        if(s.buildings[i].id!=id) continue;
        Building b = s.buildings[i];

        set_block(s.map, b.tile.x, b.tile.y, b.w, b.h, false);

        // refund jen pokud nebyla dokončená
        if(refund && b.state != BuildState::Complete){
            s.gold += b.cost_gold;
            s.wood += b.cost_wood;
        }

        // pokud to byl hotový Dropoff, smaž i z registru dropoffů
        if(b.state == BuildState::Complete && b.kind == BuildingKind::Dropoff){
            auto it = std::find_if(s.dropoffs.begin(), s.dropoffs.end(),
                [&](const Vec2i& d){ return d.x==b.tile.x && d.y==b.tile.y; });
            if(it != s.dropoffs.end()) s.dropoffs.erase(it);
        }

        s.buildings.erase(s.buildings.begin()+i);
        return;
    }
}

static inline int IDX(int w,int x,int y){ return y*w + x; }

void init_resources_from_tiles(Sim& sim, int wood_amount, int gold_amount){
    auto& m = sim.map;
    m.res_kind.resize(m.width * m.height);
    m.res_amount.resize(m.width * m.height);
    m.res_max.resize(m.width * m.height); // NEW

    for(int y=0;y<m.height;++y){
        for(int x=0;x<m.width;++x){
            int i = IDX(m.width,x,y);
            uint8_t t = m.tiles[i];
            if(t == 3){ // forest
                m.res_kind[i]   = (uint8_t)ResourceKind::Wood;
                m.res_amount[i] = wood_amount;
                m.res_max[i]    = wood_amount;
            } else if(t == 2){ // gold
                m.res_kind[i]   = (uint8_t)ResourceKind::Gold;
                m.res_amount[i] = gold_amount;
                m.res_max[i]    = gold_amount;
            } else {
                m.res_kind[i]   = (uint8_t)ResourceKind::None;
                m.res_amount[i] = 0;
                m.res_max[i]    = 0;
            }
        }
    }
}

bool resource_take_at(Sim& sim, int x, int y, int amount, int* actually_taken){
    auto& m = sim.map;
    if(x<0||y<0||x>=m.width||y>=m.height) return false;
    int i = IDX(m.width,x,y);
    if(m.res_kind[i] == (uint8_t)ResourceKind::None || m.res_amount[i] <= 0) return false;

    int take = std::min(amount, m.res_amount[i]);
    m.res_amount[i] -= take;
    if(actually_taken) *actually_taken = take;

    if(m.res_amount[i] <= 0){
        // vyčerpáno → změň na trávu a odblokuj
        m.tiles[i]    = 0;
        m.blocked[i]  = 0;
        m.res_kind[i] = (uint8_t)ResourceKind::None;
        m.res_amount[i] = 0;
    }
    return true;
}

static void unit_step(Sim& s, Unit& e, uint32_t dt_ms){
    if(!e.path.empty()){
        auto next=e.path.front();
        if(!walkable(s.map,next.x,next.y)){ e.path.clear(); return; }
        e.tile=next; e.path.erase(e.path.begin());
        return;
    }
    if(e.job==UnitJob::Moving){ e.job=UnitJob::Idle; return; }

    auto mine_logic = [&](ResourceKind kind){
        // najdi sousední resource dlaždici daného typu
        Vec2i res;
        if(!find_adjacent_resource_at(s, e.tile.x, e.tile.y, kind, &res)){
            // nebylo nic poblíž => najdi nejbližší zdroj a běž k němu
            Vec2i stand;
            if(find_nearest_resource(s, e.tile, kind, &res, &stand)){
                e.goal=stand; e.path.clear();
                astar_find(s.map, e.tile, e.goal, e.path, true);
            }else{
                e.job=UnitJob::Idle; // došlo
            }
            return;
        }

        // odeber trochu suroviny z resource dlaždice
        int taken=0;
        resource_take_at(s, res.x, res.y, 2, &taken);
        e.carried += taken;

        // Pokud se res vyčerpal, hned si najdi další
        int i = idx(s.map.width,res.x,res.y);
        if(s.map.res_amount[i] <= 0){
            Vec2i nextRes, stand;
            if(find_nearest_resource(s, e.tile, kind, &nextRes, &stand)){
                e.goal=stand; e.path.clear();
                astar_find(s.map, e.tile, e.goal, e.path, true);
            }else{
                // nic nezbylo – když něco nese, ať to alespoň doručí
                if(e.carried>0){
                    Vec2i d = nearest_dropoff(s, e.tile);
                    e.goal=d; e.path.clear();
                    astar_find(s.map, e.tile, e.goal, e.path, true);
                    e.job=UnitJob::Delivering;
                }else{
                    e.job=UnitJob::Idle;
                }
            }
            return;
        }

        // kapacita -> doručit
        if(e.carried >= 20){
            Vec2i d = nearest_dropoff(s, e.tile);
            e.goal = d; e.path.clear();
            astar_find(s.map, e.tile, e.goal, e.path, true);
            e.job = UnitJob::Delivering;
        }
    };

    if(e.job==UnitJob::GatheringGold){ mine_logic(ResourceKind::Gold); return; }
    if(e.job==UnitJob::GatheringWood){ mine_logic(ResourceKind::Wood); return; }

    if(e.job==UnitJob::Delivering){
        Vec2i d = nearest_dropoff(s, e.tile);
        if(e.tile.x==d.x && e.tile.y==d.y){
            if(e.carried_kind==1) s.gold += e.carried;
            else if(e.carried_kind==2) s.wood += e.carried;
            e.carried = 0;
            // po doručení se automaticky vrať k nejbližšímu zdroji stejného typu
            if(e.carried_kind==1 || e.carried_kind==2){
                ResourceKind kind = (e.carried_kind==1)?ResourceKind::Gold:ResourceKind::Wood;
                Vec2i res, stand;
                if(find_nearest_resource(s, e.tile, kind, &res, &stand)){
                    e.job = (kind==ResourceKind::Gold)?UnitJob::GatheringGold:UnitJob::GatheringWood;
                    e.goal = stand; e.path.clear();
                    astar_find(s.map, e.tile, e.goal, e.path, true);
                    return;
                }
            }
            // nic nenašel -> idle
            e.carried_kind=0;
            e.job = UnitJob::Idle;
        }
        return;
    }
}


bool queue_train(Sim& s, Building& b, const std::string& unit_id, int count){
    if(b.kind!=BuildingKind::Barracks) return false;
    auto it = s.unit_type_index.find(unit_id);
    if(it==s.unit_type_index.end()) return false;
    const UnitType& ut = s.unit_types[it->second];
    for(int i=0;i<count;i++){
        if(s.gold < ut.cost_gold || s.wood < ut.cost_wood || (s.food_used+ut.food) > s.food_cap) return (i>0);
        s.gold -= ut.cost_gold; s.wood -= ut.cost_wood; s.food_used += ut.food;
        b.queue.push_back(TrainItem{ it->second, ut.build_time_ms });
    }
    return true;
}

bool cancel_last_train(Sim& s, Building& b){
    if(b.queue.empty()) return false;
    TrainItem it = b.queue.back();
    const UnitType& ut = s.unit_types[it.unit_type];
    // refund costs and food
    s.gold += ut.cost_gold; s.wood += ut.cost_wood; s.food_used -= ut.food;
    b.queue.pop_back();
    return true;
}

bool cancel_train_at(Sim& s, Building& b, size_t idx){
    if(b.kind!=BuildingKind::Barracks || b.queue.empty()) return false;
    if(idx >= b.queue.size()) return false;
    const UnitType& ut = s.unit_types[b.queue[idx].unit_type];
    s.gold += ut.cost_gold; s.wood += ut.cost_wood; s.food_used -= ut.food;
    b.queue.erase(b.queue.begin()+idx);
    return true;
}

void step(Sim& s, uint32_t dt_ms){
    for(auto& e: s.units) unit_step(s,e,dt_ms);

    // Buildings: construction and queues
    for(auto& b: s.buildings){
        // construction
        int adj_workers = 0;
        for(auto& u: s.units){
            if(u.type_index!=0) continue; // worker
            // worker assigned if targeting this building
            bool assigned = (u.job==UnitJob::Building && u.building_target==b.id);
            if(!assigned) continue;
            // adjacent?
            int dx = std::max(std::max(b.tile.x - u.tile.x, 0), u.tile.x - (b.tile.x + b.w - 1));
            int dy = std::max(std::max(b.tile.y - u.tile.y, 0), u.tile.y - (b.tile.y + b.h - 1));
            if (std::max(dx, dy) == 1) adj_workers++;
        }
        if(b.state!=BuildState::Complete && adj_workers>0){
            b.state = BuildState::Constructing;
            float speed = 1.0f + 0.75f*(adj_workers-1);
            b.build_progress_ms += (int)(dt_ms * speed);
            if (b.build_progress_ms >= b.build_total_ms){
                b.state = BuildState::Complete;
                if(b.kind==BuildingKind::Dropoff){
                    s.dropoffs.push_back({b.tile.x,b.tile.y});
                    s.map.tiles[idx(s.map.width,b.tile.x,b.tile.y)] = 4;
                    // >>> umožni stát na dropoffu (aby šlo doručovat)
                    // dropoff je 1x1, proto odblokuj tuhle jednu tile
                    for (int j=0;j<b.h;++j) for (int i=0;i<b.w;++i)
                        s.map.blocked[idx(s.map.width, b.tile.x+i, b.tile.y+j)] = 0;
                }else if(b.kind==BuildingKind::Farm){
                    s.food_cap += 4;
                }
            }
        }

        // Production
        if (b.kind == BuildingKind::Barracks && b.state == BuildState::Complete && !b.queue.empty()) {
            b.queue.front().remaining_ms -= (int)dt_ms;
            if (b.queue.front().remaining_ms <= 0) {
                // spawn unit near footprint
                int ut = b.queue.front().unit_type;
                Vec2i spawn = b.tile; bool found = false;
                for (int ring = 0; ring < 4 && !found; ++ring) {
                    for (int y = b.tile.y - 1 - ring; y <= b.tile.y + b.h + ring; ++y) {
                        for (int x = b.tile.x - 1 - ring; x <= b.tile.x + b.w + ring; ++x) {
                            if (!in_bounds(s.map, x, y)) continue;
                            if (walkable(s.map, x, y)) { spawn = { x, y }; found = true; break; }
                        }
                        if (found) break;
                    }
                }

                // VYTVOŘENÍ PROMĚNNÉ uid (tady vznikne!)
                UnitId uid = spawn_unit(s, s.unit_types[ut].id, spawn.x, spawn.y);

                // RALLY POINT: rovnou pošleme jednotku na rally, pokud je nastaven
                if (b.rally.x >= 0 && b.rally.y >= 0) {
                    order_move(s, uid, b.rally.x, b.rally.y);
                }

                b.queue.pop_front();
            }
        }
    }
}

Building* find_building(Sim& s, BuildingId id){
    for(auto& b: s.buildings) if(b.id==id) return &b;
    return nullptr;
}
const Building* find_building(const Sim& s, BuildingId id){
    for(auto& b: s.buildings) if(b.id==id) return &b;
    return nullptr;
}

bool load_data(Sim& s, const std::string& assets_path){
    bool ok1 = load_units_csv(assets_path + "/units.csv", s.unit_types, s.unit_type_index);
    bool ok2 = load_map_txt(assets_path + "/map.txt", s.map);
    if(!ok1 || !ok2) return false;
    // initial dropoffs from map
    for(int y=0;y<s.map.height;++y)for(int x=0;x<s.map.width;++x)
        if(s.map.tiles[idx(s.map.width,x,y)]==4) s.dropoffs.push_back({x,y});
    // spawn worker and footman near first drop
    Vec2i d = s.dropoffs.empty()?Vec2i{1,1}:s.dropoffs[0];
    (void)spawn_unit(s, "worker", d.x+1, d.y);
    (void)spawn_unit(s, "footman", d.x+3, d.y);
    return true;
}

// =========================
// Save / Load (VERSION 1)
// =========================
static void write_line(std::ofstream& f, const std::string& s){ f << s << "\n"; }

bool save_game(const Sim& s, const std::string& path){
    std::ofstream f(path, std::ios::trunc);
    if(!f) return false;

    write_line(f, "VERSION 1");
    // ekonomika & id
    write_line(f, "ECO " + std::to_string(s.gold) + " " + std::to_string(s.wood) + " " +
                    std::to_string(s.food_used) + " " + std::to_string(s.food_cap));
    write_line(f, "IDS " + std::to_string(s.next_unit_id) + " " + std::to_string(s.next_building_id));

    // mapa
    write_line(f, "MAP " + std::to_string(s.map.width) + " " + std::to_string(s.map.height));

    // tiles
    write_line(f, "TILES");
    for(int y=0;y<s.map.height;++y){
        std::ostringstream row;
        for(int x=0;x<s.map.width;++x){
            if(x) row << ' ';
            row << int(s.map.tiles[idx(s.map.width,x,y)]);
        }
        write_line(f, row.str());
    }
    // blocked
    write_line(f, "BLOCKED");
    for(int y=0;y<s.map.height;++y){
        std::ostringstream row;
        for(int x=0;x<s.map.width;++x){
            if(x) row << ' ';
            row << int(s.map.blocked[idx(s.map.width,x,y)]);
        }
        write_line(f, row.str());
    }
    // resource kind
    write_line(f, "RESKIND");
    for(int y=0;y<s.map.height;++y){
        std::ostringstream row;
        for(int x=0;x<s.map.width;++x){
            if(x) row << ' ';
            row << int(s.map.res_kind[idx(s.map.width,x,y)]);
        }
        write_line(f, row.str());
    }
    // resource amount
    write_line(f, "RESAMNT");
    for(int y=0;y<s.map.height;++y){
        std::ostringstream row;
        for(int x=0;x<s.map.width;++x){
            if(x) row << ' ';
            row << s.map.res_amount[idx(s.map.width,x,y)];
        }
        write_line(f, row.str());
    }
    write_line(f, "RESMAX");
    for(int y=0;y<s.map.height;++y){
        std::ostringstream row;
        for(int x=0;x<s.map.width;++x){
            if(x) row << ' ';
            row << s.map.res_max[idx(s.map.width,x,y)];
        }
        write_line(f, row.str());
    }

    // dropoffy
    write_line(f, "DROPOFFS " + std::to_string(s.dropoffs.size()));
    for(auto d : s.dropoffs) write_line(f, "D " + std::to_string(d.x) + " " + std::to_string(d.y));

    // budovy
    write_line(f, "BUILDINGS " + std::to_string(s.buildings.size()));
    for(const auto& b : s.buildings){
        write_line(f, "B " + std::to_string(b.id) + " " + std::to_string((int)b.kind) + " " +
                       std::to_string(b.tile.x) + " " + std::to_string(b.tile.y) + " " +
                       std::to_string(b.w) + " " + std::to_string(b.h) + " " +
                       std::to_string((int)b.state) + " " +
                       std::to_string(b.build_progress_ms) + " " + std::to_string(b.build_total_ms) + " " +
                       std::to_string(b.cost_gold) + " " + std::to_string(b.cost_wood) + " " +
                       std::to_string(b.rally.x) + " " + std::to_string(b.rally.y));
        write_line(f, "BQ " + std::to_string(b.queue.size()));
        for(const auto& qi : b.queue){
            const std::string& uid = s.unit_types[qi.unit_type].id;
            write_line(f, "QI " + uid + " " + std::to_string(qi.remaining_ms));
        }
    }

    // jednotky
    write_line(f, "UNITS " + std::to_string(s.units.size()));
    for(const auto& u : s.units){
        const std::string& utype = s.unit_types[u.type_index].id;
        write_line(f, "U " + std::to_string(u.id) + " " + utype + " " +
                       std::to_string(u.tile.x) + " " + std::to_string(u.tile.y) + " " +
                       std::to_string(u.goal.x) + " " + std::to_string(u.goal.y) + " " +
                       std::to_string(u.hp) + " " + std::to_string((int)u.job) + " " +
                       std::to_string(u.carried) + " " + std::to_string((int)u.carried_kind) + " " +
                       std::to_string((int)u.building_target));
    }
    return true;
}

bool load_game(Sim& s, const std::string& path){
    std::ifstream f(path);
    if(!f) return false;

    Sim ns; // dočasný nový stav (pro bezpečné načtení)
    ns.unit_types = s.unit_types;
    ns.unit_type_index = s.unit_type_index;

    std::string line, tag;
    int width=0, height=0;

    if(!std::getline(f,line)) return false;
    { std::istringstream iss(line); if(!(iss>>tag) || tag!="VERSION") return false; int ver=0; iss>>ver; if(ver!=1) return false; }

    while(std::getline(f,line)){
        if(line.empty()) continue;
        std::istringstream iss(line); iss >> tag;
        if(tag=="ECO"){
            iss >> ns.gold >> ns.wood >> ns.food_used >> ns.food_cap;
        }else if(tag=="IDS"){
            iss >> ns.next_unit_id >> ns.next_building_id;
        }else if(tag=="MAP"){
            iss >> width >> height;
            ns.map.width=width; ns.map.height=height;
            ns.map.tiles.assign(width*height,0);
            ns.map.blocked.assign(width*height,0);
            ns.map.res_kind.assign(width*height,0);
            ns.map.res_amount.assign(width*height,0);
        }else if(tag=="TILES"){
            for(int y=0;y<height;++y){
                std::getline(f,line); std::istringstream rs(line);
                for(int x=0;x<width;++x){ int v; rs>>v; ns.map.tiles[idx(width,x,y)]=(uint8_t)v; }
            }
        }else if(tag=="BLOCKED"){
            for(int y=0;y<height;++y){
                std::getline(f,line); std::istringstream rs(line);
                for(int x=0;x<width;++x){ int v; rs>>v; ns.map.blocked[idx(width,x,y)]=(uint8_t)v; }
            }
        }else if(tag=="RESKIND"){
            for(int y=0;y<height;++y){
                std::getline(f,line); std::istringstream rs(line);
                for(int x=0;x<width;++x){ int v; rs>>v; ns.map.res_kind[idx(width,x,y)]=(uint8_t)v; }
            }
        }else if(tag=="RESAMNT"){
            for(int y=0;y<height;++y){
                std::getline(f,line); std::istringstream rs(line);
                for(int x=0;x<width;++x){ int v; rs>>v; ns.map.res_amount[idx(width,x,y)]=v; }
            }
        }else if(tag=="RESMAX"){
            for(int y=0;y<height;++y){
                std::getline(f,line); std::istringstream rs(line);
                for(int x=0;x<width;++x){ int v; rs>>v; ns.map.res_max[idx(width,x,y)]=v; }
            }
        }else if(tag=="DROPOFFS"){
            int n=0; iss>>n;
            for(int i=0;i<n;++i){
                std::getline(f,line); std::istringstream ds(line); std::string t; ds>>t; // "D"
                Vec2i d{}; ds>>d.x>>d.y; ns.dropoffs.push_back(d);
            }
        }else if(tag=="BUILDINGS"){
            int n=0; iss>>n;
            for(int i=0;i<n;++i){
                std::getline(f,line); std::istringstream bs(line); std::string t; bs>>t; // "B"
                Building b{};
                int kind, state;
                bs >> b.id >> kind >> b.tile.x >> b.tile.y >> b.w >> b.h >> state
                   >> b.build_progress_ms >> b.build_total_ms >> b.cost_gold >> b.cost_wood
                   >> b.rally.x >> b.rally.y;
                b.kind = (BuildingKind)kind;
                b.state = (BuildState)state;
                ns.buildings.push_back(b);

                // queue hlavička
                std::getline(f,line); std::istringstream qh(line); std::string tq; int qn=0; qh>>tq>>qn;
                for(int k=0;k<qn;++k){
                    std::getline(f,line); std::istringstream qi(line); std::string qtag, uid; int rem=0; qi>>qtag>>uid>>rem;
                    auto it = ns.unit_type_index.find(uid);
                    if(it!=ns.unit_type_index.end()) ns.buildings.back().queue.push_back(TrainItem{ (uint16_t)it->second, rem });
                }
            }
        }else if(tag=="UNITS"){
            int n=0; iss>>n;
            for(int i=0;i<n;++i){
                std::getline(f,line); std::istringstream us(line); std::string t, uid;
                Unit u{}; int job, ck;
                us>>t>>u.id>>uid>>u.tile.x>>u.tile.y>>u.goal.x>>u.goal.y>>u.hp>>job>>u.carried>>ck>>u.building_target;
                auto it = ns.unit_type_index.find(uid);
                if(it==ns.unit_type_index.end()) continue;
                u.type_index = (uint16_t)it->second;
                u.job = (UnitJob)job;
                u.carried_kind = (uint8_t)ck;
                ns.units.push_back(u);
            }
        }
    }

    s = std::move(ns); // přepiš běžící stav
    return true;
}