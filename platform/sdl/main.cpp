#include <SDL.h>
#include <SDL_mixer.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <SDL_ttf.h>
#include "sim.hpp"
#include "pathfinding.hpp"

static const int WINDOW_W = 1152;
static const int WINDOW_H = 720;
static const int ISO_W = 48;
static const int ISO_H = 24;
static const int UNIT_W = 32;
static const int UNIT_H = 40;
static int ORIGIN_X = 260;
static int ORIGIN_Y = 120;

// camera pixel offset
static int cam_px = 0;
static int cam_py = 0;

enum class BuildMode { None, Dropoff, Farm, Barracks };

struct Puff{ int x,y; uint32_t t0; }; // screen center, start time

static uint64_t now_us(){
    return (uint64_t)SDL_GetPerformanceCounter() * 1000000ull / (uint64_t)SDL_GetPerformanceFrequency();
}

static inline SDL_Point iso_to_screen_px(int tx,int ty){
    int sx = (tx - ty) * (ISO_W/2) - cam_px + ORIGIN_X;
    int sy = (tx + ty) * (ISO_H/2) - cam_py + ORIGIN_Y;
    return {sx, sy};
}
static inline Vec2i screen_px_to_iso(int sx,int sy){
    float fx = (float)(sx + cam_px - ORIGIN_X) / (ISO_W/2);
    float fy = (float)(sy + cam_py - ORIGIN_Y) / (ISO_H/2);
    int tx = (int)std::floor((fx + fy) * 0.5f + 0.0f);
    int ty = (int)std::floor((fy - fx) * 0.5f + 0.0f);
    return {tx, ty};
}

static void clamp_camera(const Sim& sim){
    int max_px_x = (sim.map.width + sim.map.height) * (ISO_W/2);
    int max_px_y = (sim.map.width + sim.map.height) * (ISO_H/2);
    if(cam_px < -WINDOW_W/2) cam_px = -WINDOW_W/2;
    if(cam_py < 0) cam_py = 0;
    if(cam_px > max_px_x) cam_px = max_px_x;
    if(cam_py > max_px_y) cam_py = max_px_y;
}

// minimalist number drawing
static void draw_digit(SDL_Renderer* r, int x, int y, int d){
    static const uint8_t glyphs[10][5]={
        {0b111,0b101,0b101,0b101,0b111},
        {0b010,0b110,0b010,0b010,0b111},
        {0b111,0b001,0b111,0b100,0b111},
        {0b111,0b001,0b111,0b001,0b111},
        {0b101,0b101,0b111,0b001,0b001},
        {0b111,0b100,0b111,0b001,0b111},
        {0b111,0b100,0b111,0b101,0b111},
        {0b111,0b001,0b001,0b001,0b001},
        {0b111,0b101,0b111,0b101,0b111},
        {0b111,0b101,0b111,0b001,0b111}
    };
    SDL_Rect px{0,0,2,2};
    for(int row=0;row<5;++row){
        for(int col=0;col<3;++col){
            if(glyphs[d][row] & (1<<(2-col))){
                px.x = x + col*3; px.y = y + row*3;
                SDL_RenderFillRect(r,&px);
            }
        }
    }
}
static void draw_number(SDL_Renderer* r, int x, int y, int v){
    if(v==0){ draw_digit(r,x,y,0); return; }
    int tmp=v; int digits[12]; int n=0;
    while(tmp>0 && n<12){ digits[n++]=tmp%10; tmp/=10; }
    for(int i=0;i<n;++i){ draw_digit(r, x+(n-1-i)*10, y, digits[n-1-i]); }
}

static inline int idx(int w, int x, int y){ return y*w + x; }
// ---------- Command card buttons ----------

struct Btn {
    SDL_Rect r; int iconIndex; bool enabled; const char* label; const char* hotkey; int costGold; int costWood;
};

static bool point_in(const SDL_Rect& r, int mx, int my){ return mx>=r.x && my>=r.y && mx<r.x+r.w && my<r.y+r.h; }

static SDL_Rect icon_src_32(int idx){ return SDL_Rect{ (idx%8)*32, (idx/8)*32, 32, 32 }; }

static void draw_cc_btn(SDL_Renderer* ren, SDL_Texture* texIcons, const Btn& b) {
    SDL_SetRenderDrawColor(ren, b.enabled ? 42:24, b.enabled ? 68:24, b.enabled ? 42:24, 255);
    SDL_RenderFillRect(ren, &b.r);
    SDL_SetRenderDrawColor(ren, 110,140,190,255);
    SDL_RenderDrawRect(ren, &b.r);
    SDL_Rect idst{ b.r.x+(b.r.w-32)/2, b.r.y+(b.r.h-32)/2, 32, 32 };
    SDL_Rect isrc = icon_src_32(b.iconIndex);
    SDL_RenderCopy(ren, texIcons, &isrc, &idst);
}

static void draw_text(SDL_Renderer* ren, TTF_Font* font, const char* text, int x, int y, SDL_Color col){
    if(!font || !text) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text, col);
    if(!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
    SDL_Rect dst{ x, y, s->w, s->h };
    SDL_RenderCopy(ren, t, nullptr, &dst);
    SDL_FreeSurface(s);
    SDL_DestroyTexture(t);
}

static void draw_build_tooltip(SDL_Renderer* ren, TTF_Font* font, int mx, int my,
                               const char* label, const char* hotkey, int gold, int wood, bool affordable) {
    char line1[96]; char line2[96];
    std::snprintf(line1, sizeof(line1), "%s [%s]", label?label:"", hotkey?hotkey:"");
    std::snprintf(line2, sizeof(line2), "Gold: %d   Wood: %d", gold, wood);

    int w = 180, h = 56;
    SDL_Rect tip{ mx+14, my+14, w, h };
    SDL_SetRenderDrawColor(ren, 20,24,32,230); SDL_RenderFillRect(ren, &tip);
    SDL_SetRenderDrawColor(ren, affordable? 80:140, affordable? 200:80, 80, 255); SDL_RenderDrawRect(ren, &tip);

    if (font){
        draw_text(ren, font, line1, tip.x+8, tip.y+6, SDL_Color{220,220,220,255});
        draw_text(ren, font, line2, tip.x+8, tip.y+28, SDL_Color{200,200,200,255});
    }
}

static inline bool rect_contains(SDL_Rect r, int x, int y){
    return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h;
}

static inline SDL_Rect src_for_tile(uint8_t t, int x, int y){
    int col=0;
    if(t==0){ col = ( ((unsigned)(x*73856093u) ^ (unsigned)(y*19349663u)) & 1u ) ? 5:0; }
    else if(t==1){ col=1; }
    else if(t==2){ col=2; }
    else if(t==3){ col=3; }
    else if(t==4){ col=4; }
    return SDL_Rect{ col*ISO_W, 0, ISO_W, ISO_H };
}

static inline SDL_Rect src_for_unit(const Unit& u){
    int frame = (SDL_GetTicks()/180)%3;
    int base = (u.type_index==0)?0:3;
    int col = base + frame;
    return SDL_Rect{ col*UNIT_W, 0, UNIT_W, UNIT_H };
}

static inline SDL_Rect src_for_build(BuildingKind k){
    int col = (k==BuildingKind::Dropoff)?0: (k==BuildingKind::Farm?1:2);
    return SDL_Rect{ col*64, 0, 64, 48 };
}

static inline BuildingId get_selected_building_id(const Sim& s){
    for(const auto& b: s.buildings) if(b.selected) return b.id;
    return 0;
}

static inline SDL_Rect src_for_resource(uint8_t t, int variant){
    // t==3 -> strom, t==2 -> zlato
    // columns: 0..2 trees, 3..4 gold
    int col = 0;
    if(t==3) col = std::min(2, variant % 3);           // 0..2
    else     col = 3 + std::min(1, variant % 2);       // 3..4
    return SDL_Rect{ col*64, 0, 64, 64 };
}

// ------------

int main(int argc, char** argv){
    (void)argc; (void)argv;
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0){
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    Mix_OpenAudio(44100, AUDIO_S16, 2, 1024);

    SDL_Window* win = SDL_CreateWindow("RTS SDL2 (UI++ & Production)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return 1;
    }
    TTF_Font* font = TTF_OpenFont("assets/DejaVuSans.ttf", 14);
    if (!font) {
        std::fprintf(stderr, "TTF_OpenFont failed: %s\n", TTF_GetError());
        // nepřerušuji běh, ale texty se nebudou kreslit
    }

    // Assets
    SDL_Surface* surfRes  = SDL_LoadBMP("assets/iso_resources.bmp"); // 5 columns, 64x64
    SDL_Surface* surfTiles = SDL_LoadBMP("assets/iso_tiles.bmp");     // 6 columns
    SDL_Surface* surfUnits = SDL_LoadBMP("assets/iso_units_3f.bmp");  // 6 columns
    SDL_Surface* surfEdges = SDL_LoadBMP("assets/iso_edges.bmp");     // 4 columns
    SDL_Surface* surfBuild = SDL_LoadBMP("assets/iso_buildings3.bmp");// 3 columns 64x48
    SDL_Surface* surfIcons = SDL_LoadBMP("assets/ui_icons.bmp");      // 32x32 grid (0:gold,1:wood,2:drop,3:farm,4:barr,5:footman,6:cancel,7:plus)
    if(!surfTiles || !surfUnits || !surfEdges || !surfBuild || !surfIcons || !surfRes){
        std::fprintf(stderr,"Failed to load bmp assets.\n");
        return 2;
    }
    Uint32 mag = SDL_MapRGB(surfTiles->format,255,0,255);
    SDL_SetColorKey(surfTiles, SDL_TRUE, mag);
    SDL_SetColorKey(surfUnits, SDL_TRUE, mag);
    SDL_SetColorKey(surfEdges, SDL_TRUE, mag);
    SDL_SetColorKey(surfBuild, SDL_TRUE, mag);
    SDL_SetColorKey(surfIcons, SDL_TRUE, SDL_MapRGB(surfIcons->format,255,0,255));
    SDL_SetColorKey(surfRes, SDL_TRUE, SDL_MapRGB(surfRes->format,255,0,255));
    SDL_Texture* texTiles = SDL_CreateTextureFromSurface(ren, surfTiles);
    SDL_Texture* texUnits = SDL_CreateTextureFromSurface(ren, surfUnits);
    SDL_Texture* texEdges = SDL_CreateTextureFromSurface(ren, surfEdges);
    SDL_Texture* texBuild = SDL_CreateTextureFromSurface(ren, surfBuild);
    SDL_Texture* texIcons = SDL_CreateTextureFromSurface(ren, surfIcons);
    SDL_Texture* texRes  = SDL_CreateTextureFromSurface(ren, surfRes);
    SDL_FreeSurface(surfTiles); SDL_FreeSurface(surfUnits); SDL_FreeSurface(surfEdges); SDL_FreeSurface(surfBuild); SDL_FreeSurface(surfIcons); SDL_FreeSurface(surfRes);

    Sim sim;
    if(!load_data(sim, "assets")){
        std::fprintf(stderr, "Failed to load assets.\n"); return 3;
    }

    init_resources_from_tiles(sim, /*wood*/300, /*gold*/500);

    const double TICK = 1.0 / (double)sim.cfg.tick_rate;
    uint64_t last = now_us(); double acc=0.0;
    bool running=true, dragging=false; SDL_Rect dragRect{0,0,0,0};
    Vec2i hover{-1,-1};

    BuildMode bmode = BuildMode::None;

    // UI layout
    SDL_Rect topbar{0,0,WINDOW_W,64};
    SDL_Rect cmdCard{WINDOW_W-330, WINDOW_H-220, 320, 210};      // command card 3x3 vpravo dole
    SDL_Rect selPanel{8, WINDOW_H-220, WINDOW_W-8-340, 210};     // široký selection panel vlevo dole
    SDL_Rect queuePanel{WINDOW_W-330, WINDOW_H-260, 320, 36};

    // command buttons grid
    SDL_Rect cardBtns[9];
    for(int i=0;i<9;++i){
        int cx = i%3, cy=i/3;
        cardBtns[i] = SDL_Rect{ cmdCard.x+12 + cx*100, cmdCard.y+12 + cy*64, 92, 56 };
    }

    // minimap
    int mm_w=220, mm_h=140, mm_x=WINDOW_W-mm_w-10, mm_y=6;

    // selection state
    auto clear_selection=[&](Sim& s){
        for(auto& u: s.units) u.selected=false;
        for(auto& b: s.buildings) b.selected=false;
    };
    auto any_workers_selected=[&](const Sim& s)->bool{
        for(const auto& u: s.units) if(u.selected && u.type_index==0) return true;
        return false;
    };

    while(running){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT) running=false;
            if(e.type==SDL_KEYDOWN){
                if(e.key.keysym.sym==SDLK_ESCAPE){ if(bmode!=BuildMode::None) bmode=BuildMode::None; else clear_selection(sim); }
                if(e.key.keysym.sym==SDLK_a) cam_px -= 24;
                if(e.key.keysym.sym==SDLK_d) cam_px += 24;
                if(e.key.keysym.sym==SDLK_w) cam_py -= 12;
                if(e.key.keysym.sym==SDLK_s) cam_py += 12;

                BuildingId sbid = get_selected_building_id(sim);
                if(e.key.keysym.sym==SDLK_DELETE && sbid){
                    cancel_building(sim, sbid, true);
                }

                // Hotkeys
                if(any_workers_selected(sim)){
                    if(e.key.keysym.sym==SDLK_1) bmode=BuildMode::Dropoff;
                    if(e.key.keysym.sym==SDLK_2) bmode=BuildMode::Farm;
                    if(e.key.keysym.sym==SDLK_3) bmode=BuildMode::Barracks;
                }else if(sbid){
                    Building* b = find_building(sim, sbid);
                    if(b && b->kind==BuildingKind::Barracks && b->state==BuildState::Complete){
                        if(e.key.keysym.sym==SDLK_q) queue_train(sim, *b, "footman", 1);
                        if(e.key.keysym.sym==SDLK_w) queue_train(sim, *b, "footman", 5);
                        if(e.key.keysym.sym==SDLK_e) cancel_last_train(sim, *b);
                    }
                }
            }
            if(e.type==SDL_MOUSEMOTION){
                hover = screen_px_to_iso(e.motion.x, e.motion.y);
                // Edge scroll
                const int margin=18;
                if(e.motion.x<margin) cam_px -= 8;
                if(e.motion.x>WINDOW_W-margin) cam_px += 8;
                if(e.motion.y<margin) cam_py -= 4;
                if(e.motion.y>WINDOW_H-margin) cam_py += 4;
            }

            static SDL_Rect dragRectLocal{0,0,0,0};
            static bool draggingLocal=false;

            if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT){
                draggingLocal=true; dragRectLocal={e.button.x,e.button.y,0,0};

                BuildingId sbid = get_selected_building_id(sim);
                // command card clicks
                if(sbid){
                    Building* b = find_building(sim, sbid);
                    if(b && b->kind==BuildingKind::Barracks && b->state==BuildState::Complete){
                        if(rect_contains(cardBtns[0], e.button.x, e.button.y)) queue_train(sim, *b, "footman", 1);
                        if(rect_contains(cardBtns[1], e.button.x, e.button.y)) queue_train(sim, *b, "footman", 5);
                        if(rect_contains(cardBtns[2], e.button.x, e.button.y)) cancel_last_train(sim, *b);
                    }
                }else if(any_workers_selected(sim)){
                    if(rect_contains(cardBtns[0], e.button.x, e.button.y)) bmode=BuildMode::Dropoff;
                    if(rect_contains(cardBtns[1], e.button.x, e.button.y)) bmode=BuildMode::Farm;
                    if(rect_contains(cardBtns[2], e.button.x, e.button.y)) bmode=BuildMode::Barracks;
                }
            }
            if(e.type==SDL_MOUSEMOTION && draggingLocal){
                dragRectLocal.w = e.motion.x - dragRectLocal.x;
                dragRectLocal.h = e.motion.y - dragRectLocal.y;
            }
            if(e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_LEFT){
                // if small drag, treat as click
                bool isClick = (std::abs(e.button.x-dragRectLocal.x)<4 && std::abs(e.button.y-dragRectLocal.y)<4);
                draggingLocal=false;

                // Minimap click-to-pan
                if(e.button.x >= mm_x && e.button.x < mm_x+mm_w && e.button.y >= mm_y && e.button.y < mm_y+mm_h){
                    float nx = float(e.button.x - mm_x)/float(mm_w);
                    float ny = float(e.button.y - mm_y)/float(mm_h);
                    cam_px = int(nx * (sim.map.width+sim.map.height) * (ISO_W/2) - WINDOW_W/2);
                    cam_py = int(ny * (sim.map.width+sim.map.height) * (ISO_H/2) - WINDOW_H/4);
                }else if(bmode!=BuildMode::None){
                    Vec2i t = screen_px_to_iso(e.button.x, e.button.y);
                    auto bt = get_btype(bmode==BuildMode::Dropoff?BuildingKind::Dropoff: (bmode==BuildMode::Farm?BuildingKind::Farm:BuildingKind::Barracks));
                    if(can_place_building(sim, bt, t.x, t.y) && (sim.gold >= bt.cost_gold && sim.wood >= bt.cost_wood)){
                        BuildingId bid = start_building(sim, bt, t.x, t.y);
                        if(bid){
                            // assign selected workers to build
                            for(auto& u: sim.units){
                                if(u.selected && u.type_index==0){
                                    u.job = UnitJob::Building;
                                    u.building_target = bid;
                                    // move next to footprint
                                    Vec2i best = u.tile; int bestd=1e9; bool found=false;
                                    for(int yy=t.y-1; yy<=t.y+bt.h; ++yy){
                                        for(int xx=t.x-1; xx<=t.x+bt.w; ++xx){
                                            bool onEdge = !(xx>=t.x && xx<t.x+bt.w && yy>=t.y && yy<t.y+bt.h);
                                            if(onEdge && xx>=0 && yy>=0 && xx<sim.map.width && yy<sim.map.height){
                                                if(!sim.map.blocked[idx(sim.map.width,xx,yy)] && sim.map.tiles[idx(sim.map.width,xx,yy)]!=1){
                                                    int d=std::abs(xx-u.tile.x)+std::abs(yy-u.tile.y);
                                                    if(d<bestd){ bestd=d; best={xx,yy}; found=true; }
                                                }
                                            }
                                        }
                                    }
                                    if(found){
                                        u.goal=best; u.path.clear();
                                        astar_find(sim.map, u.tile, u.goal, u.path, true);
                                    }
                                }
                            }
                        }
                    }
                }else{
                    if(isClick){
                        // click selection logic: units first, else building
                        bool selectedSomething=false;
                        // unit hit by rectangle around sprite
                        for(auto& u: sim.units){
                            SDL_Point p = iso_to_screen_px(u.tile.x, u.tile.y);
                            SDL_Rect ub = { p.x-UNIT_W/2, p.y-UNIT_H+ISO_H/2, UNIT_W, UNIT_H };
                            if(rect_contains(ub, e.button.x, e.button.y)){
                                if(!(SDL_GetModState() & KMOD_SHIFT)) clear_selection(sim);
                                u.selected=true; selectedSomething=true; break;
                            }
                        }
                        if(!selectedSomething){
                            Vec2i t = screen_px_to_iso(e.button.x, e.button.y);
                            // search buildings
                            for(auto& b: sim.buildings){
                                SDL_Point c = iso_to_screen_px(b.tile.x, b.tile.y);
                                SDL_Rect sprite = { c.x - 32, c.y - 40 - (b.w==2 ? 12 : 0), 64, 48 }; // přesně jak kreslíš
                                if(rect_contains(sprite, e.button.x, e.button.y)){
                                    clear_selection(sim);
                                    b.selected = true;
                                    // POZOR: nemáš proměnnou selectedBuilding -> nic nesetuj, stačí flag
                                    selectedSomething = true;
                                    break;
                                }
                            }
                        }
                        if(!selectedSomething) clear_selection(sim);
                    }else{
                        // drag select units
                        SDL_Rect r=dragRectLocal; if(r.w<0){ r.x+=r.w; r.w=-r.w; } if(r.h<0){ r.y+=r.h; r.h=-r.h; }
                        if(!(SDL_GetModState() & KMOD_SHIFT)) clear_selection(sim);
                        for(auto& u: sim.units){
                            SDL_Point p = iso_to_screen_px(u.tile.x, u.tile.y);
                            SDL_Rect ub = { p.x-UNIT_W/2, p.y-UNIT_H+ISO_H/2, UNIT_W, UNIT_H };
                            SDL_Rect inter;
                            if(SDL_IntersectRect(&r,&ub,&inter)) u.selected=true;
                        }
                    }
                }
            }
            if(e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_RIGHT){
                // RMB: queue cancel click? (above cmd card)
                bool queueHit=false;
                BuildingId sbid = get_selected_building_id(sim);
                if(sbid){
                    Building* b = find_building(sim, sbid);
                    if(b && b->kind==BuildingKind::Barracks && b->state==BuildState::Complete && !b->queue.empty()){
                        int qx = queuePanel.x+8; int qy=queuePanel.y+8;
                        for(size_t i=0;i<b->queue.size() && i<10;i++){
                            SDL_Rect box{ qx + int(i)*28, qy, 24, 24 };
                            if(rect_contains(box, e.button.x, e.button.y)){ cancel_last_train(sim, *b); queueHit=true; break; }
                        }
                    }
                }
                if(queueHit) continue;

                // else: order
                Vec2i t = screen_px_to_iso(e.button.x, e.button.y);
                // if clicking on building and it is not complete -> assign workers
                Building* hit=nullptr;
                for(auto& b: sim.buildings){
                    SDL_Point c = iso_to_screen_px(b.tile.x, b.tile.y);
                    SDL_Rect sprite = { c.x - 32, c.y - 40 - (b.w==2 ? 12 : 0), 64, 48 };
                    if(rect_contains(sprite, e.button.x, e.button.y)) { hit = &b; break; }
                }
                if(hit && hit->state!=BuildState::Complete){
                    for(auto& u: sim.units) if(u.selected && u.type_index==0){ u.job=UnitJob::Building; u.building_target=hit->id; }
                }else{
                    for(auto& u: sim.units) if(u.selected){
                        uint8_t rk = (t.x>=0 && t.y>=0 && t.x<sim.map.width && t.y<sim.map.height)
                        ? sim.map.res_kind[idx(sim.map.width,t.x,t.y)]
                        : (uint8_t)ResourceKind::None;
                        if(u.type_index==0 && (rk==(uint8_t)ResourceKind::Gold || rk==(uint8_t)ResourceKind::Wood))
                            order_gather(sim,u.id,t.x,t.y);
                        else
                            order_move(sim,u.id,t.x,t.y);
                    }
                }
            }
        }

        clamp_camera(sim);

        uint64_t tnow = now_us(); acc += (double)(tnow-last)/1000000.0; last=tnow;
        while(acc >= (1.0 / (double)sim.cfg.tick_rate)){ step(sim, (uint32_t)(1000.0 / (double)sim.cfg.tick_rate)); acc -= (1.0 / (double)sim.cfg.tick_rate); }

        SDL_SetRenderDrawColor(ren, 10, 12, 16, 255); SDL_RenderClear(ren);

        // Tiles
        for(int y=0;y<sim.map.height;++y){
            for(int x=0;x<sim.map.width;++x){
                SDL_Point c = iso_to_screen_px(x,y);
                SDL_Rect src = src_for_tile(sim.map.tiles[idx(sim.map.width,x,y)], x, y);
                SDL_Rect dst = { c.x - ISO_W/2, c.y - ISO_H/2, ISO_W, ISO_H };
                SDL_RenderCopy(ren, texTiles, &src, &dst);
            }
        }

        // Resources (trees & gold) — draw after ground, before buildings
        for(int y=0; y<sim.map.height; ++y){
            for(int x=0; x<sim.map.width; ++x){
                uint8_t rk = sim.map.res_kind[idx(sim.map.width,x,y)];
                if(rk==(uint8_t)ResourceKind::Gold || rk==(uint8_t)ResourceKind::Wood){
                    SDL_Point c = iso_to_screen_px(x,y);
                    unsigned h = (unsigned)(x*73856093u) ^ (unsigned)(y*19349663u);
                    int variant = (int)(h & 7);
                    // t = 3 pro strom, t = 2 pro zlato (jen pro výběr správné části spritesheetu)
                    uint8_t t = (rk==(uint8_t)ResourceKind::Wood) ? 3 : 2;
                    SDL_Rect s = src_for_resource(t, variant);
                    SDL_Rect d = { c.x - 32, c.y - 56, 64, 64 };
                    SDL_RenderCopy(ren, texRes, &s, &d);
                }
            }
        }

        // Buildings
        for(const auto& b: sim.buildings){
            SDL_Point c = iso_to_screen_px(b.tile.x, b.tile.y);
            SDL_Rect src = src_for_build(b.kind);
            SDL_Rect dst = { c.x - 32, c.y - 40, 64, 48 };
            if(b.w==2) dst.y -= 12;
            SDL_RenderCopy(ren, texBuild, &src, &dst);
            // selection outline
            if(b.selected){
                SDL_SetRenderDrawColor(ren, 240,240,240,220);
                for(int j=0;j<b.h;++j) for(int i=0;i<b.w;++i){
                    SDL_Point p = iso_to_screen_px(b.tile.x+i, b.tile.y+j);
                    SDL_RenderDrawLine(ren, p.x, p.y-ISO_H/2, p.x+ISO_W/2, p.y);
                    SDL_RenderDrawLine(ren, p.x+ISO_W/2, p.y, p.x, p.y+ISO_H/2);
                    SDL_RenderDrawLine(ren, p.x, p.y+ISO_H/2, p.x-ISO_W/2, p.y);
                    SDL_RenderDrawLine(ren, p.x-ISO_W/2, p.y, p.x, p.y-ISO_H/2);
                }
            }
            // construction bar
            if(b.state!=BuildState::Complete){
                int barw = 44;
                float t = b.build_total_ms? (float)b.build_progress_ms/(float)b.build_total_ms : 0.f;
                if(t<0) t=0; if(t>1) t=1;
                SDL_Rect bg{ dst.x+10, dst.y-12, barw, 6 };
                SDL_Rect fg{ dst.x+10, dst.y-12, (int)(barw*t), 6 };
                SDL_SetRenderDrawColor(ren,50,50,50,220); SDL_RenderFillRect(ren,&bg);
                SDL_SetRenderDrawColor(ren,80,200,120,220); SDL_RenderFillRect(ren,&fg);
            }
        }

        // Units (pseudodepth sort)
        std::vector<int> ord(sim.units.size()); for(size_t i=0;i<ord.size();++i) ord[i]=(int)i;
        std::sort(ord.begin(), ord.end(), [&](int a,int b){
            if(sim.units[a].tile.y != sim.units[b].tile.y) return sim.units[a].tile.y < sim.units[b].tile.y;
            return sim.units[a].tile.x < sim.units[b].tile.x;
        });
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        for(int ui: ord){
            const auto& u = sim.units[ui];
            SDL_Point c = iso_to_screen_px(u.tile.x, u.tile.y);
            SDL_SetRenderDrawColor(ren,0,0,0,80);
            SDL_Rect shadow = { c.x-14, c.y-6, 28, 12 };
            SDL_RenderFillRect(ren,&shadow);
            SDL_Rect src = src_for_unit(u);
            SDL_Rect dst = { c.x-UNIT_W/2, c.y-UNIT_H+ISO_H/2, UNIT_W, UNIT_H };
            SDL_RenderCopy(ren, texUnits, &src, &dst);
            if(u.selected){
                SDL_SetRenderDrawColor(ren,255,255,255,200);
                SDL_Point p = c;
                SDL_RenderDrawLine(ren, p.x, p.y-ISO_H/2, p.x+ISO_W/2, p.y);
                SDL_RenderDrawLine(ren, p.x+ISO_W/2, p.y, p.x, p.y+ISO_H/2);
                SDL_RenderDrawLine(ren, p.x, p.y+ISO_H/2, p.x-ISO_W/2, p.y);
                SDL_RenderDrawLine(ren, p.x-ISO_W/2, p.y, p.x, p.y-ISO_H/2);
            }
        }

        // Ghost building preview
        if(bmode!=BuildMode::None){
            auto bt = get_btype(bmode==BuildMode::Dropoff?BuildingKind::Dropoff: (bmode==BuildMode::Farm?BuildingKind::Farm:BuildingKind::Barracks));
            Vec2i t = hover;
            bool ok = (t.x>=0 && t.y>=0 && t.x+bt.w<=sim.map.width && t.y+bt.h<=sim.map.height)
                       && can_place_building(sim, bt, t.x, t.y);
            bool enough = (sim.gold >= bt.cost_gold && sim.wood >= bt.cost_wood);
            SDL_Point c = iso_to_screen_px(t.x, t.y);
            SDL_Rect src = src_for_build(bt.kind);
            SDL_Rect dst = { c.x - 32, c.y - 40, 64, 48 };
            if(bt.w==2) dst.y -= 12;
            SDL_SetTextureAlphaMod(texBuild, ok?160:80);
            SDL_SetTextureColorMod(texBuild, ok?80:200, ok?200:80, 80);
            SDL_RenderCopy(ren, texBuild, &src, &dst);
            SDL_SetTextureAlphaMod(texBuild, 255);
            SDL_SetTextureColorMod(texBuild, 255,255,255);

            // tooltip: icony + čísla (momentálně jen box bez textu)
            SDL_Rect tip{ c.x+36, c.y-68, 160, 64 };
            SDL_SetRenderDrawColor(ren,20,20,28,230); SDL_RenderFillRect(ren,&tip);
            SDL_SetRenderDrawColor(ren, (ok && enough)? 80:140, (ok && enough)? 200:80, 80, 255);
            SDL_RenderDrawRect(ren,&tip);

            // ikony + čísla (ponecháš)
            SDL_Rect icG{ tip.x+6, tip.y+8, 16, 16 };
            SDL_Rect srcG = icon_src_32(0); SDL_RenderCopy(ren, texIcons, &srcG, &icG);
            SDL_SetRenderDrawColor(ren,230,230,230,255); draw_number(ren, tip.x+26, tip.y+10, bt.cost_gold);

            SDL_Rect icW{ tip.x+6, tip.y+30, 16, 16 };
            SDL_Rect srcW = icon_src_32(1); SDL_RenderCopy(ren, texIcons, &srcW, &icW);
            SDL_SetRenderDrawColor(ren,230,230,230,255); draw_number(ren, tip.x+26, tip.y+32, bt.cost_wood);

            // text s důvodem (pokud je font)
            if (font){
                if (!ok)      draw_text(ren, font, "Blocked / Not grass", tip.x+72, tip.y+10, SDL_Color{230,120,120,255});
                else if(!enough) draw_text(ren, font, "Not enough resources", tip.x+72, tip.y+10, SDL_Color{230,120,120,255});
                else            draw_text(ren, font, "Place: OK", tip.x+72, tip.y+10, SDL_Color{160,220,160,255});
            }
        }

        // ---------- UI LAYERS ----------
        // Topbar
        SDL_SetRenderDrawColor(ren,18,20,28,255); SDL_RenderFillRect(ren,&topbar);
        // resources
        {
            SDL_Rect icG{12,16,24,24};
            SDL_Rect icW{96,16,24,24};

            SDL_Rect srcGold = icon_src_32(0);
            SDL_Rect srcWood = icon_src_32(1);

            SDL_RenderCopy(ren, texIcons, &srcGold, &icG);
            SDL_RenderCopy(ren, texIcons, &srcWood, &icW);

            SDL_SetRenderDrawColor(ren,230,230,230,255); draw_number(ren, 40, 22, sim.gold);
            SDL_SetRenderDrawColor(ren,230,230,230,255); draw_number(ren, 124, 22, sim.wood);
            SDL_SetRenderDrawColor(ren,220,220,220,255); draw_number(ren, 190, 22, sim.food_used);
            SDL_SetRenderDrawColor(ren,180,220,180,255); draw_number(ren, 228, 22, sim.food_cap);
        }

        // Selection panel
        SDL_SetRenderDrawColor(ren,14,16,22,255); SDL_RenderFillRect(ren,&selPanel);
        SDL_SetRenderDrawColor(ren,60,80,110,255); SDL_RenderDrawRect(ren,&selPanel);
        {
            int selUnits=0; for(auto& u: sim.units) if(u.selected) selUnits++;
            BuildingId sbid = get_selected_building_id(sim);
            if(sbid){
                const Building* b = find_building(sim, sbid);
                if(b){
                    SDL_Rect box{ selPanel.x+8, selPanel.y+8, 120, 90 };
                    SDL_SetRenderDrawColor(ren,20,24,32,255); SDL_RenderFillRect(ren,&box);
                    SDL_SetRenderDrawColor(ren,60,80,110,255); SDL_RenderDrawRect(ren,&box);
                    SDL_Rect src = src_for_build(b->kind);
                    SDL_Rect dst{ box.x+(box.w-64)/2, box.y+10, 64, 48 };
                    SDL_RenderCopy(ren, texBuild, &src, &dst);
                    // construction status
                    if(b->state!=BuildState::Complete){
                        SDL_Rect bg{ box.x+12, box.y+68, 96, 6 };
                        float rt = b->build_total_ms? (float)b->build_progress_ms/(float)b->build_total_ms : 0.f;
                        SDL_Rect fg{ box.x+12, box.y+68, (int)(96*rt), 6 };
                        SDL_SetRenderDrawColor(ren,50,50,50,220); SDL_RenderFillRect(ren,&bg);
                        SDL_SetRenderDrawColor(ren,80,200,120,220); SDL_RenderFillRect(ren,&fg);
                    }
                }
            }else{
                SDL_SetRenderDrawColor(ren,230,230,230,255); draw_number(ren, selPanel.x+12, selPanel.y+12, selUnits);
            }
        }

        // Command card
        SDL_SetRenderDrawColor(ren,14,16,22,255); SDL_RenderFillRect(ren,&cmdCard);
        SDL_SetRenderDrawColor(ren,60,80,110,255); SDL_RenderDrawRect(ren,&cmdCard);

        {
            const bool workersSel = any_workers_selected(sim);
            BuildingId sbid = get_selected_building_id(sim);

            if(workersSel){
                // affordability
                bool canDrop  = (sim.gold >= 120);
                bool canFarm  = (sim.gold >= 60 && sim.wood >= 40);
                bool canBarr  = (sim.gold >= 150 && sim.wood >= 80);

                Btn b0{cardBtns[0], 2, canDrop, "Drop-off", "1", 120, 0};
                Btn b1{cardBtns[1], 3, canFarm, "Farm",     "2", 60, 40};
                Btn b2{cardBtns[2], 4, canBarr, "Barracks", "3", 150, 80};

                draw_cc_btn(ren, texIcons, b0);
                draw_cc_btn(ren, texIcons, b1);
                draw_cc_btn(ren, texIcons, b2);

                int mx,my; SDL_GetMouseState(&mx,&my);
                if(point_in(b0.r, mx,my)) draw_build_tooltip(ren, font, mx,my, b0.label,b0.hotkey,b0.costGold,b0.costWood,b0.enabled);
                if(point_in(b1.r, mx,my)) draw_build_tooltip(ren, font, mx,my, b1.label,b1.hotkey,b1.costGold,b1.costWood,b1.enabled);
                if(point_in(b2.r, mx,my)) draw_build_tooltip(ren, font, mx,my, b2.label,b2.hotkey,b2.costGold,b2.costWood,b2.enabled);
            } else if(sbid){
                const Building* b = find_building(sim, sbid);
                if(b && b->kind==BuildingKind::Barracks && b->state==BuildState::Complete){
                    bool canFoot = (sim.gold >= 100 && sim.food_used+1 <= sim.food_cap);
                    // „plus x5“ ber jako dostupné, ale reálně zakládej postupně s kontrolou v queue_train
                    Btn bf{cardBtns[0], 5, canFoot, "Footman", "Q", 100, 0};
                    Btn bp{cardBtns[1], 7, true,    "x5",      "W", 0, 0};
                    Btn bc{cardBtns[2], 6, true,    "Cancel",  "E", 0, 0};
                    draw_cc_btn(ren, texIcons, bf);
                    draw_cc_btn(ren, texIcons, bp);
                    draw_cc_btn(ren, texIcons, bc);

                    int mx,my; SDL_GetMouseState(&mx,&my);
                    if(point_in(bf.r, mx,my)) draw_build_tooltip(ren, font, mx,my, bf.label,bf.hotkey,bf.costGold,bf.costWood,bf.enabled);
                    if(point_in(bp.r, mx,my)) draw_build_tooltip(ren, font, mx,my, bp.label,bp.hotkey,0,0,true);
                    if(point_in(bc.r, mx,my)) draw_build_tooltip(ren, font, mx,my, bc.label,bc.hotkey,0,0,true);
                } else {
                    for(int i=0;i<9;i++){ Btn bx{cardBtns[i], 7, false, "", "", 0,0}; draw_cc_btn(ren, texIcons, bx); }
                }
            } else {
                for(int i=0;i<9;i++){ Btn bx{cardBtns[i], 7, false, "", "", 0,0}; draw_cc_btn(ren, texIcons, bx); }
            }
        }

        // Queue panel (for barracks)
        SDL_SetRenderDrawColor(ren,14,16,22,255); SDL_RenderFillRect(ren,&queuePanel);
        SDL_SetRenderDrawColor(ren,60,80,110,255); SDL_RenderDrawRect(ren,&queuePanel);
        {
            BuildingId sbid = get_selected_building_id(sim);
            if(sbid){
                const Building* b = find_building(sim, sbid);
                if(b && b->kind==BuildingKind::Barracks){
                    int qx = queuePanel.x+8; int qy=queuePanel.y+8;
                    for(size_t i=0;i<b->queue.size() && i<10;i++){
                        SDL_Rect box{ qx + int(i)*28, qy, 24, 24 };
                        SDL_SetRenderDrawColor(ren, 30,30,40,255); SDL_RenderFillRect(ren,&box);
                        SDL_SetRenderDrawColor(ren, 200,80,80,255); SDL_RenderDrawRect(ren,&box);
                        SDL_Rect idst{ box.x+4, box.y+4, 16, 16 };
                        SDL_Rect isrc = icon_src_32(5); // footman head
                        SDL_RenderCopy(ren, texIcons, &isrc, &idst);
                    }
                }
            }
        }

        // Minimap
        {
            SDL_Rect mm_bg{mm_x-2, mm_y-2, mm_w+4, mm_h+4};
            SDL_SetRenderDrawColor(ren, 8, 8, 12, 220); SDL_RenderFillRect(ren, &mm_bg);
            for(int y=0;y<sim.map.height;++y){
                for(int x=0;x<sim.map.width;++x){
                    uint8_t t = sim.map.tiles[idx(sim.map.width,x,y)];
                    Uint8 r=50,g=80,b=50;
                    if(t==1){ r=80; g=80; b=90; }
                    else if(t==2){ r=160; g=140; b=40; }
                    else if(t==3){ r=30; g=120; b=50; }
                    else if(t==4){ r=130; g=90; b=50; }
                    int px = mm_x + x*mm_w/sim.map.width;
                    int py = mm_y + y*mm_h/sim.map.height;
                    SDL_SetRenderDrawColor(ren,r,g,b,255);
                    SDL_RenderDrawPoint(ren, px, py);
                }
            }
            for(const auto& b: sim.buildings){
                int px = mm_x + b.tile.x*mm_w/sim.map.width;
                int py = mm_y + b.tile.y*mm_h/sim.map.height;
                if(b.kind==BuildingKind::Dropoff) SDL_SetRenderDrawColor(ren,200,180,60,255);
                else if(b.kind==BuildingKind::Farm) SDL_SetRenderDrawColor(ren,120,180,120,255);
                else SDL_SetRenderDrawColor(ren,160,120,220,255);
                SDL_RenderDrawPoint(ren,px,py);
                SDL_RenderDrawPoint(ren,px+1,py);
                SDL_RenderDrawPoint(ren,px,py+1);
            }
            for(const auto& u: sim.units){
                int px = mm_x + u.tile.x*mm_w/sim.map.width;
                int py = mm_y + u.tile.y*mm_h/sim.map.height;
                if(u.selected) SDL_SetRenderDrawColor(ren,255,255,255,255);
                else if(u.type_index==0) SDL_SetRenderDrawColor(ren,90,150,255,255);
                else SDL_SetRenderDrawColor(ren,230,80,80,255);
                SDL_RenderDrawPoint(ren,px,py);
                SDL_RenderDrawPoint(ren,px+1,py);
                SDL_RenderDrawPoint(ren,px,py+1);
            }
        }

        SDL_RenderPresent(ren);
        SDL_Delay(1);
    }

    SDL_DestroyTexture(texTiles); SDL_DestroyTexture(texUnits); SDL_DestroyTexture(texEdges); SDL_DestroyTexture(texBuild); SDL_DestroyTexture(texIcons); SDL_DestroyTexture(texRes); 

    if (font) TTF_CloseFont(font);
        TTF_Quit();

    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    Mix_CloseAudio(); SDL_Quit(); return 0;
}
