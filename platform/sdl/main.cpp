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


static void draw_text(SDL_Renderer* ren, TTF_Font* font,
                      const char* text, int x, int y, SDL_Color col);

// --- UI helpers ---
static inline void sdl_fill(SDL_Renderer* r, SDL_Rect rc, Uint8 R, Uint8 G, Uint8 B, Uint8 A){
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, R,G,B,A);
    SDL_RenderFillRect(r, &rc);
}
static inline void sdl_rect(SDL_Renderer* r, SDL_Rect rc, Uint8 R, Uint8 G, Uint8 B, Uint8 A){
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, R,G,B,A);
    SDL_RenderDrawRect(r, &rc);
}
static inline void bevel_box(SDL_Renderer* r, SDL_Rect rc, SDL_Color fill, SDL_Color dark, SDL_Color light, Uint8 a=255){
    sdl_fill(r, rc, fill.r, fill.g, fill.b, a);
    sdl_rect(r, rc, dark.r, dark.g, dark.b, a);
    SDL_SetRenderDrawColor(r, light.r, light.g, light.b, a);
    SDL_RenderDrawLine(r, rc.x, rc.y, rc.x+rc.w-1, rc.y);
    SDL_RenderDrawLine(r, rc.x, rc.y, rc.x, rc.y+rc.h-1);
}
static inline void fake_gradient(SDL_Renderer* r, SDL_Rect rc, SDL_Color base, Uint8 aTop, Uint8 aBot){
    SDL_Rect top = {rc.x, rc.y, rc.w, rc.h/2};
    SDL_Rect bot = {rc.x, rc.y+rc.h/2, rc.w, rc.h - rc.h/2};
    sdl_fill(r, top, base.r, base.g, base.b, aTop);
    sdl_fill(r, bot, base.r, base.g, base.b, aBot);
}
static inline void fill_bar(SDL_Renderer* r, SDL_Rect rc, float t, SDL_Color bg, SDL_Color fg){
    if(t<0) t=0; if(t>1) t=1;
    sdl_fill(r, rc, bg.r,bg.g,bg.b,220);
    SDL_Rect fill = rc; fill.w = (int)(rc.w * t);
    sdl_fill(r, fill, fg.r,fg.g,fg.b,240);
}

static inline void cc_btn_hover(SDL_Renderer* r, const SDL_Rect& rc, bool hovered){
    if(!hovered) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 255,255,255,30);
    SDL_RenderFillRect(r, &rc);
    SDL_SetRenderDrawColor(r, 200,200,240,180);
    SDL_RenderDrawRect(r, &rc);
}
// malý badge s hotkeyem v rohu
static inline void cc_badge(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const char* k){
    if(!k || !*k) return;
    SDL_Rect dot{ rc.x+rc.w-16, rc.y+4, 12, 12 };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 30,34,46,230); SDL_RenderFillRect(r,&dot);
    SDL_SetRenderDrawColor(r, 120,150,190,255); SDL_RenderDrawRect(r,&dot);
    if(font) draw_text(r, font, k, dot.x+3, dot.y-1, SDL_Color{220,220,230,255});
}

static const int WINDOW_W = 1152;
static const int WINDOW_H = 720;
static const int ISO_W = 48;
static const int ISO_H = 24;
static const int UNIT_W = 32;
static const int UNIT_H = 40;
static int ORIGIN_X = 260;
static int ORIGIN_Y = 120;

static bool g_minimapDragging = false;

// camera pixel offset
static int cam_px = 0;
static int cam_py = 0;

// ZOOM (globální)
static float g_zoom = 1.0f;
static constexpr float ZOOM_MIN = 0.5f;
static constexpr float ZOOM_MAX = 2.0f;
static constexpr float ZOOM_STEP = 1.1f; // 10 % na krok

// --- UI rect snapshots for input logic ---
static SDL_Rect g_ui_topbar  {0,0,0,0};
static SDL_Rect g_ui_selPanel{0,0,0,0};
static SDL_Rect g_ui_cmdCard {0,0,0,0};
static SDL_Rect g_ui_queue   {0,0,0,0};
static SDL_Rect g_ui_minimap {0,0,0,0};

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
    int world_w = (sim.map.width + sim.map.height) * (ISO_W/2);
    int world_h = (sim.map.width + sim.map.height) * (ISO_H/2);
    int min_x = -ORIGIN_X;
    int min_y = -ORIGIN_Y;
    int max_x = world_w - (WINDOW_W - ORIGIN_X);
    int max_y = world_h - (WINDOW_H/2);
    cam_px = std::clamp(cam_px, min_x, max_x);
    cam_py = std::clamp(cam_py, min_y, max_y);
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
    SDL_Rect rc = b.r;

    // podklad + bevel
    sdl_fill(ren, rc, 18,20,28,255);
    fake_gradient(ren, rc, SDL_Color{26,30,40,255}, 70, 120);
    bevel_box(ren, rc, SDL_Color{0,0,0,0}, SDL_Color{60,80,110,255}, SDL_Color{120,150,190,150}, 255);

    // ikona
    SDL_Rect idst{ rc.x+(rc.w-32)/2, rc.y+(rc.h-32)/2, 32, 32 };
    SDL_Rect isrc = icon_src_32(b.iconIndex);

    if(!b.enabled) SDL_SetTextureColorMod(texIcons, 160,160,160);
    SDL_RenderCopy(ren, texIcons, &isrc, &idst);
    if(!b.enabled) SDL_SetTextureColorMod(texIcons, 255,255,255);

    // (volitelně) malý “disabled” závoj
    if(!b.enabled){
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0,0,0,80);
        SDL_RenderFillRect(ren, &rc);
    }
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

// --- HUD notice (file-scope, s fade-out) ---
struct HudNotice {
    std::string text;
    uint32_t show_ms = 0;    // kdy se začal ukazovat
    uint32_t dur_ms  = 0;    // celková doba zobrazení
};

inline void hud_show(HudNotice& h, const std::string& t, uint32_t dur_ms){
    h.text   = t;
    h.show_ms = SDL_GetTicks();
    h.dur_ms  = dur_ms;
}

inline uint8_t hud_alpha(const HudNotice& h, uint32_t now){
    if(h.text.empty() || h.dur_ms==0) return 0;
    uint32_t elapsed = (now >= h.show_ms ? now - h.show_ms : 0);
    if(elapsed >= h.dur_ms) return 0;
    // fade posledních 350 ms
    const uint32_t fade_ms = 350;
    if(elapsed > h.dur_ms - fade_ms){
        float t = float(h.dur_ms - elapsed) / float(fade_ms); // 1..0
        int a = int(220 * t);
        if(a<0) a=0; if(a>220) a=220;
        return (uint8_t)a;
    }
    return 220; // plná “viditelnost” před fadem
}

struct ResourcePopup {
    int tile_x = -1, tile_y = -1; // kde v dlaždicích
    uint8_t kind = 0;             // ResourceKind
    uint32_t until_ms = 0;        // kdy zmizí (SDL_GetTicks)
};
static ResourcePopup g_resPopup;

inline bool point_in_rect(const SDL_Rect& r, int x, int y){
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

inline bool over_any_ui(int x, int y){
    // podle potřeby přidej/uber panely, které máš
    return point_in_rect(g_ui_topbar,  x,y)
        || point_in_rect(g_ui_selPanel,x,y)
        || point_in_rect(g_ui_cmdCard, x,y)
        || point_in_rect(g_ui_queue,   x,y)
        || point_in_rect(g_ui_minimap, x,y);
}

// --- Anti-clipping helpers ---

// Clamp UI rect do okna (UI je v 1× měřítku)
static inline void clamp_ui_rect_to_window(SDL_Rect& r, int margin = 0){
    if(r.x < margin) r.x = margin;
    if(r.y < margin) r.y = margin;
    if(r.x + r.w > WINDOW_W - margin) r.x = WINDOW_W - margin - r.w;
    if(r.y + r.h > WINDOW_H - margin) r.y = WINDOW_H - margin - r.h;
}

// Clamp WORLD rect do okna s ohledem na renderer scale (g_zoom).
// 'r' je ve world pixelech (tj. co posíláš do SDL_RenderFillRect před resetem Scale).
static inline void clamp_world_rect_to_window(SDL_Rect& r, float scale, int margin = 0){
    // spočti obrazovkové souřadnice po scale
    int sx = int(r.x * scale);
    int sy = int(r.y * scale);
    int sw = int(r.w * scale);
    int sh = int(r.h * scale);

    int dx = 0, dy = 0;
    if(sx < margin) dx = margin - sx;
    if(sy < margin) dy = margin - sy;
    if(sx + sw > WINDOW_W - margin) dx = (WINDOW_W - margin) - (sx + sw);
    if(sy + sh > WINDOW_H - margin) dy = (WINDOW_H - margin) - (sy + sh);

    if(dx || dy){
        // převeď posun zpět do "world" pixelů (přičti tak, aby se vešlo i po scale)
        // ceil/floor pro jistotu, ať nic nepřeběhne přes okraj
        r.x += (dx >= 0) ? int(std::ceil(dx / scale)) : int(std::floor(dx / scale));
        r.y += (dy >= 0) ? int(std::ceil(dy / scale)) : int(std::floor(dy / scale));
    }
}

// Iso tile outline (světová měřítka – kreslit při scale g_zoom)
static inline void draw_iso_tile_outline(SDL_Renderer* r, int tx, int ty){
    SDL_Point p0 = iso_to_screen_px(tx,   ty);
    SDL_Point p1 = iso_to_screen_px(tx+1, ty);
    SDL_Point p2 = iso_to_screen_px(tx+1, ty+1);
    SDL_Point p3 = iso_to_screen_px(tx,   ty+1);
    SDL_RenderDrawLine(r, p0.x, p0.y, p1.x, p1.y);
    SDL_RenderDrawLine(r, p1.x, p1.y, p2.x, p2.y);
    SDL_RenderDrawLine(r, p2.x, p2.y, p3.x, p3.y);
    SDL_RenderDrawLine(r, p3.x, p3.y, p0.x, p0.y);
}

int main(int argc, char** argv){
    (void)argc; (void)argv;
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0){
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024);
    Mix_Chunk* sfxSave = Mix_LoadWAV("assets/sfx/sfx_save.wav");
    Mix_Chunk* sfxLoad = Mix_LoadWAV("assets/sfx/sfx_load.wav");

    SDL_Window* win = SDL_CreateWindow("RTS SDL2 - Strategie",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
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
    auto dbg_tex = [&](const char* name, SDL_Texture* t){
        int w=0,h=0; SDL_QueryTexture(t,nullptr,nullptr,&w,&h);
        std::printf("[TEX] %s: %dx%d\n", name, w, h);
    };
    dbg_tex("iso_tiles",  texTiles);
    dbg_tex("iso_units",  texUnits);
    dbg_tex("iso_edges",  texEdges);
    dbg_tex("iso_builds", texBuild);
    dbg_tex("ui_icons",   texIcons);
    dbg_tex("resources",  texRes);
    SDL_FreeSurface(surfTiles); SDL_FreeSurface(surfUnits); SDL_FreeSurface(surfEdges); SDL_FreeSurface(surfBuild); SDL_FreeSurface(surfIcons); SDL_FreeSurface(surfRes);

    Sim sim;
    if(!load_data(sim, "assets")){
        std::fprintf(stderr, "Failed to load assets.\n"); return 3;
    }

    init_resources_from_tiles(sim, /*wood*/300, /*gold*/500);

    const double TICK = 1.0 / (double)sim.cfg.tick_rate;
    uint64_t last = now_us(); double acc=0.0;
    bool running=true;
    Vec2i hover{-1,-1};

    static SDL_Rect dragRectLocal{0,0,0,0};
    static bool     draggingLocal = false;

    HudNotice hud{};

    uint32_t autosave_period_ms = 5 * 60 * 1000; // 5 minut
    uint32_t next_autosave_ms   = SDL_GetTicks() + autosave_period_ms;

    BuildMode bmode = BuildMode::None;

    // --- Resource info popup (UI dole) ---
    struct ResourceInfo {
        int index = -1;          // index dlaždice v mapě (pro live update)
        uint8_t kind = 0;        // ResourceKind::None/Gold/Wood
        uint32_t until_ms = 0;   // kdy zmizí (SDL_GetTicks)
    };
    static ResourceInfo g_resInfo;

    // UI layout
    const int UI_TOPBAR_H = 48;
    const int UI_PANEL_H  = 132;
    const int UI_SPACING  = 8;

    SDL_Rect topbar     = { 0, 0, WINDOW_W, UI_TOPBAR_H };
    SDL_Rect selPanel   = { UI_SPACING, WINDOW_H - UI_PANEL_H, 220, UI_PANEL_H };
    SDL_Rect cmdCard    = { selPanel.x + selPanel.w + UI_SPACING, WINDOW_H - UI_PANEL_H, 240, UI_PANEL_H };
    SDL_Rect queuePanel = { cmdCard.x  + cmdCard.w  + UI_SPACING, WINDOW_H - UI_PANEL_H, 220, UI_PANEL_H };
    SDL_Rect minimap    = { WINDOW_W - 180 - UI_SPACING, WINDOW_H - 180 - UI_SPACING, 180, 180 };

    // snapshot pro vstupní logiku
    g_ui_topbar   = topbar;
    g_ui_selPanel = selPanel;
    g_ui_cmdCard  = cmdCard;
    g_ui_queue    = queuePanel;
    g_ui_minimap  = minimap;

    // command buttons grid
    SDL_Rect cardBtns[9];
    {
        const int P     = 12;     // vnitřní padding panelu
        const int GAP_X = 8;      // vodorovná mezera mezi tlačítky
        const int GAP_Y = 8;      // svislá mezera
        const int COLS  = 3;
        const int ROWS  = 3;

        const int innerW = cmdCard.w - 2*P;
        const int innerH = cmdCard.h - 2*P;

        const int btnW = (innerW - (COLS-1)*GAP_X) / COLS;
        const int btnH = (innerH - (ROWS-1)*GAP_Y) / ROWS;

        for(int i=0;i<9;++i){
            int cx = i % COLS, cy = i / COLS;
            int x = cmdCard.x + P + cx*(btnW + GAP_X);
            int y = cmdCard.y + P + cy*(btnH + GAP_Y);
            cardBtns[i] = SDL_Rect{ x, y, btnW, btnH };
        }
    }

    // minimap
    int mm_w = 220, mm_h = 140;
    int mm_x = selPanel.x + 12;
    int mm_y = selPanel.y + selPanel.h - mm_h - 12;

    // selection state
    auto clear_selection=[&](Sim& s){
        for(auto& u: s.units) u.selected=false;
        for(auto& b: s.buildings) b.selected=false;
    };
    auto any_workers_selected=[&](const Sim& s)->bool{
        for(const auto& u: s.units) if(u.selected && u.type_index==0) return true;
        return false;
    };

    auto relax_builders = [&](Sim& s){
        for(auto& u : s.units){
            if(u.type_index!=0) continue;            // jen dělníci
            if(u.job != UnitJob::Building) continue;

            Building* tb = find_building(s, u.building_target);
            if(!tb || tb->state == BuildState::Complete){
                u.job = UnitJob::Idle;
                u.building_target = 0;
                u.path.clear();
            }
        }
    };

    auto world_viewport = [&](){
        SDL_Rect r{0, 0, WINDOW_W, WINDOW_H};

        // nahoře pryč topbar
        r.y = UI_TOPBAR_H;

        // dole pryč spodní panely (panel + mezera)
        const int reserved_bottom = UI_PANEL_H + UI_SPACING;
        r.h = WINDOW_H - r.y - reserved_bottom;

        return r;
    };

    while(running){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT){
                save_game(sim, "autosave.txt");
                running=false;
            }
            if(e.type==SDL_KEYDOWN){
                if(e.key.keysym.sym==SDLK_ESCAPE){ if(bmode!=BuildMode::None) bmode=BuildMode::None; else clear_selection(sim); }
                if(e.key.keysym.sym==SDLK_a) cam_px -= 24;
                if(e.key.keysym.sym==SDLK_d) cam_px += 24;
                if(e.key.keysym.sym==SDLK_w) cam_py -= 12;
                if(e.key.keysym.sym==SDLK_s) cam_py += 12;
                if(e.key.keysym.sym==SDLK_F5){
                    if(save_game(sim, "savegame.txt")){
                        std::printf("[SAVE] OK -> savegame.txt\n");
                        hud_show(hud, "Saved", 1500);
                        if (sfxSave) Mix_PlayChannel(-1, sfxSave, 0);
                    }else{
                        std::printf("[SAVE] FAILED\n");
                        hud_show(hud, "Save failed", 1500);
                    }
                }
                if(e.key.keysym.sym==SDLK_F9){
                    if(load_game(sim, "savegame.txt")){
                        std::printf("[LOAD] OK <- savegame.txt\n");
                        hud_show(hud, "Loaded", 1500);
                        if (sfxLoad) Mix_PlayChannel(-1, sfxLoad, 0);
                    }else{
                        std::printf("[LOAD] FAILED\n");
                        hud_show(hud, "Load failed", 1500);
                    }
                }

                BuildingId sbid = get_selected_building_id(sim);
                if(e.key.keysym.sym==SDLK_DELETE && sbid){
                    remove_building(sim, sbid, true);
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
                int mx = e.motion.x, my = e.motion.y;
                if (over_any_ui(mx, my)) {
                    // ... případně jen update hover/tooltipů ...
                } else {
                    // ➋ dovol edge-scroll jen, když nejsi v build módu (volitelné)
                    if (bmode == BuildMode::None) {
                        SDL_Rect vp = world_viewport();   // hranice světa v screen pixelech (bez UI)
                        const int margin = 18;

                        int dx = 0, dy = 0;
                        if (mx >= vp.x && mx <  vp.x + margin)                 dx = - (vp.x + margin - mx);
                        if (mx >  vp.x + vp.w - margin && mx <= vp.x + vp.w)   dx =   (mx - (vp.x + vp.w - margin));
                        if (my >= vp.y && my <  vp.y + margin)                 dy = - (vp.y + margin - my);
                        if (my >  vp.y + vp.h - margin && my <= vp.y + vp.h)   dy =   (my - (vp.y + vp.h - margin));

                        if (dx != 0) cam_px += (dx > 0 ? +1 : -1) * std::max(8,  std::min(24, std::abs(dx)));
                        if (dy != 0) cam_py += (dy > 0 ? +1 : -1) * std::max(4,  std::min(12, std::abs(dy)));
                    }
                }

                if(g_minimapDragging){
                    int mx = std::clamp(e.motion.x, mm_x, mm_x + mm_w - 1);
                    int my = std::clamp(e.motion.y, mm_y, mm_y + mm_h - 1);

                    float nx = (mx - mm_x) / float(mm_w);
                    float ny = (my - mm_y) / float(mm_h);

                    int world_w = (sim.map.width + sim.map.height) * (ISO_W/2);
                    int world_h = (sim.map.width + sim.map.height) * (ISO_H/2);

                    cam_px = int(nx * world_w - WINDOW_W/2);
                    cam_py = int(ny * world_h - WINDOW_H/4);
                    clamp_camera(sim);
                }

                {
                    int mx = e.motion.x, my = e.motion.y;
                    if (over_any_ui(mx,my)) {
                        hover = {-1,-1};
                    } else {
                        int ux = int(mx / g_zoom);
                        int uy = int(my / g_zoom);
                        hover = screen_px_to_iso(ux, uy);
                    }
                }
            }

            if(e.type == SDL_MOUSEWHEEL){
                int mx, my; SDL_GetMouseState(&mx, &my);

                float pre_z = g_zoom;
                // "odzoomovaná" obrazovka pro výpočet kotvy
                float nx = mx / pre_z;
                float ny = my / pre_z;

                float post_z = pre_z * (e.wheel.y > 0 ? ZOOM_STEP : 1.0f/ZOOM_STEP);
                post_z = std::clamp(post_z, ZOOM_MIN, ZOOM_MAX);

                if(fabsf(post_z - pre_z) > 1e-6f){
                    // udrž bod pod kurzorem na stejném místě
                    cam_px = int( nx - (pre_z / post_z) * (nx - cam_px) );
                    cam_py = int( ny - (pre_z / post_z) * (ny - cam_py) );
                    g_zoom = post_z;
                    clamp_camera(sim);
                }
                {
                    if (over_any_ui(mx,my)) {
                        hover = {-1,-1};
                    } else {
                        int ux = int(mx / g_zoom);
                        int uy = int(my / g_zoom);
                        hover = screen_px_to_iso(ux, uy);
                    }
                }
            }

            if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT){
                BuildingId sbid = get_selected_building_id(sim);

                // Minimap: začátek dragu + okamžitý pan
                if(e.button.x >= mm_x && e.button.x < mm_x + mm_w &&
                e.button.y >= mm_y && e.button.y < mm_y + mm_h)
                {
                    g_minimapDragging = true;

                    float nx = (e.button.x - mm_x) / float(mm_w); // 0..1
                    float ny = (e.button.y - mm_y) / float(mm_h); // 0..1

                    int world_w = (sim.map.width + sim.map.height) * (ISO_W/2);
                    int world_h = (sim.map.width + sim.map.height) * (ISO_H/2);

                    // Zaměř střed herního okna na bod z minimapy
                    cam_px = int(nx * world_w - WINDOW_W/2);
                    cam_py = int(ny * world_h - WINDOW_H/4);
                    clamp_camera(sim);

                    // Už nic dalšího pro tento LMB down (nechceme start selekčního dragu)
                    continue;
                }

                if (over_any_ui(e.button.x, e.button.y)) {
                    // klik do UI → nespouštěj výběrový drag
                    draggingLocal = false;
                } else {
                    draggingLocal = true;
                    dragRectLocal = { e.button.x, e.button.y, 0, 0 };
                }

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
                if(!over_any_ui(e.motion.x, e.motion.y)){
                    dragRectLocal.w = e.motion.x - dragRectLocal.x;
                    dragRectLocal.h = e.motion.y - dragRectLocal.y;
                }
            }
            if(e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_LEFT){
                if(g_minimapDragging){
                    g_minimapDragging = false;
                    continue; // ukončeno, nic dalšího pro tento LMB up
                }
                // if small drag, treat as click
                bool isClick = (std::abs(e.button.x-dragRectLocal.x)<4 && std::abs(e.button.y-dragRectLocal.y)<4);
                if (over_any_ui(e.button.x, e.button.y)) isClick = false;
                draggingLocal = false;

                // Minimap click-to-pan
                if(e.button.x >= mm_x && e.button.x < mm_x+mm_w && e.button.y >= mm_y && e.button.y < mm_y+mm_h){
                    float nx = float(e.button.x - mm_x)/float(mm_w);
                    float ny = float(e.button.y - mm_y)/float(mm_h);
                    cam_px = int(nx * (sim.map.width+sim.map.height) * (ISO_W/2) - WINDOW_W/2);
                    cam_py = int(ny * (sim.map.width+sim.map.height) * (ISO_H/2) - WINDOW_H/4);
                }else if(bmode!=BuildMode::None){
                    int mx = e.button.x, my = e.button.y;
                    if(over_any_ui(mx,my)) { continue; }

                    int ux = int(mx / g_zoom);
                    int uy = int(my / g_zoom);
                    Vec2i t = screen_px_to_iso(ux, uy);

                    auto bt = get_btype(
                        bmode==BuildMode::Dropoff ? BuildingKind::Dropoff :
                        bmode==BuildMode::Farm    ? BuildingKind::Farm    :
                                                    BuildingKind::Barracks
                    );

                    if(can_place_building(sim, bt, t.x, t.y) &&
                    (sim.gold >= bt.cost_gold && sim.wood >= bt.cost_wood))
                    {
                        BuildingId bid = start_building(sim, bt, t.x, t.y);
                        if(bid){
                            // přiřaď vybrané dělníky k nové stavbě
                            int ux2 = int(e.button.x / g_zoom);
                            int uy2 = int(e.button.y / g_zoom);
                            for(auto& u: sim.units){
                                if(u.selected && u.type_index==0){
                                    u.job = UnitJob::Building;
                                    u.building_target = bid;

                                    // najdi okrajové políčko kolem footprintu
                                    Vec2i best = u.tile; int bestd=1e9; bool found=false;
                                    for(int yy=t.y-1; yy<=t.y+bt.h; ++yy){
                                        for(int xx=t.x-1; xx<=t.x+bt.w; ++xx){
                                            bool onEdge = !(xx>=t.x && xx<t.x+bt.w && yy>=t.y && yy<t.y+bt.h);
                                            if(onEdge && xx>=0 && yy>=0 && xx<sim.map.width && yy<sim.map.height){
                                                if(!sim.map.blocked[idx(sim.map.width,xx,yy)] && sim.map.tiles[idx(sim.map.width,xx,yy)]!=1){
                                                    int d = std::abs(xx-u.tile.x)+std::abs(yy-u.tile.y);
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
                        bmode = BuildMode::None;
                    }
                }else{
                    if(isClick){
                        int ux = int(e.button.x / g_zoom);
                        int uy = int(e.button.y / g_zoom);
                        bool selectedSomething = false;

                        // units
                        for(auto& u: sim.units){
                            SDL_Point p = iso_to_screen_px(u.tile.x, u.tile.y);
                            SDL_Rect ub = { p.x-UNIT_W/2, p.y-UNIT_H+ISO_H/2, UNIT_W, UNIT_H };
                            if(rect_contains(ub, ux, uy)){
                                if(!(SDL_GetModState() & KMOD_SHIFT)) clear_selection(sim);
                                u.selected=true; selectedSomething=true; break;
                            }
                        }
                        if(!selectedSomething){
                            // (tady klidně tenhle řádek úplně vynech, proměnná 't' se dál nepoužívá)
                            // Vec2i t = screen_px_to_iso(ux, uy);

                            for(auto& b: sim.buildings){
                                SDL_Point c = iso_to_screen_px(b.tile.x, b.tile.y);
                                SDL_Rect sprite = { c.x - 32, c.y - 40 - (b.w==2 ? 12 : 0), 64, 48 };
                                if(rect_contains(sprite, ux, uy)){
                                    clear_selection(sim);
                                    g_resInfo.index = -1;
                                    b.selected = true;
                                    selectedSomething = true;
                                    break;
                                }
                            }
                        }
                        // --- Resource click popup (world) ---
                        if(!selectedSomething){
                            Vec2i t = screen_px_to_iso(ux, uy);
                            if(t.x>=0 && t.y>=0 && t.x<sim.map.width && t.y<sim.map.height){
                                int i = idx(sim.map.width, t.x, t.y);
                                uint8_t rk = sim.map.res_kind[i]; // Wood/Gold?
                                if(rk == (uint8_t)ResourceKind::Gold || rk == (uint8_t)ResourceKind::Wood){
                                    g_resPopup.tile_x = t.x;
                                    g_resPopup.tile_y = t.y;
                                    g_resPopup.kind   = rk;
                                    g_resPopup.until_ms = SDL_GetTicks() + 1500; // ~1.5 s
                                    // (volitelné) pokud nechceš zároveň spodní "card", tak ji rovnou skryj:
                                    // g_resInfo.index = -1;
                                }
                            }
                        }
                        if(!selectedSomething) clear_selection(sim);
                    }else{
                        // drag select units
                        SDL_Rect r = dragRectLocal;
                        if(r.w<0){ r.x+=r.w; r.w=-r.w; }
                        if(r.h<0){ r.y+=r.h; r.h=-r.h; }

                        // převod UI rectu do world pixelů (odzoomuj)
                        SDL_Rect rw{
                            int(r.x / g_zoom),
                            int(r.y / g_zoom),
                            int(r.w / g_zoom),
                            int(r.h / g_zoom)
                        };

                        if(!(SDL_GetModState() & KMOD_SHIFT)) clear_selection(sim);
                        for(auto& u: sim.units){
                            SDL_Point p = iso_to_screen_px(u.tile.x, u.tile.y);
                            SDL_Rect ub = { p.x-UNIT_W/2, p.y-UNIT_H+ISO_H/2, UNIT_W, UNIT_H };
                            SDL_Rect inter;
                            if(SDL_IntersectRect(&rw,&ub,&inter)) u.selected=true;
                        }
                    }
                }
            }
            if(e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_RIGHT){
                relax_builders(sim);
                // RMB: queue cancel click? (above cmd card)
                bool queueHit=false;
                BuildingId sbid = get_selected_building_id(sim);
                if(sbid){
                    Building* b = find_building(sim, sbid);
                    if(b && b->kind==BuildingKind::Barracks && b->state==BuildState::Complete && !b->queue.empty()){
                        int qx = queuePanel.x+8; int qy=queuePanel.y+8;
                        for(size_t i=0;i<b->queue.size() && i<10;i++){
                            SDL_Rect box{ qx + int(i)*28, qy, 24, 24 };
                            if(rect_contains(box, e.button.x, e.button.y)){ (void)cancel_train_at(sim, *b, i); queueHit=true; break; }
                        }
                    }
                }
                if(queueHit) continue;

                // SHIFT + RMB: set rally on selected Barracks
                if((SDL_GetModState() & KMOD_SHIFT)){
                    BuildingId sbid = get_selected_building_id(sim);
                    if(sbid){
                        Building* bb = find_building(sim, sbid);
                        if(bb && bb->kind==BuildingKind::Barracks){
                            int ux = int(e.button.x / g_zoom);
                            int uy = int(e.button.y / g_zoom);
                            Vec2i t = screen_px_to_iso(ux, uy);
                            bb->rally = t;
                            hud_show(hud, "Rally set", 900);
                            continue;
                        }
                    }
                }

                // else: order
                int ux = int(e.button.x / g_zoom);
                int uy = int(e.button.y / g_zoom);
                Vec2i t = screen_px_to_iso(ux, uy);

                // if clicking on building and it is not complete -> assign workers
                Building* hit=nullptr;
                for(auto& b: sim.buildings){
                    SDL_Point c = iso_to_screen_px(b.tile.x, b.tile.y);
                    SDL_Rect sprite = { c.x - 32, c.y - 40 - (b.w==2 ? 12 : 0), 64, 48 };
                    if(rect_contains(sprite, ux, uy)) { hit = &b; break; }
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
        relax_builders(sim);

        SDL_SetRenderDrawColor(ren, 10, 12, 16, 255); SDL_RenderClear(ren);

        SDL_RenderSetScale(ren, g_zoom, g_zoom); 

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

                    // procento zbývající suroviny (uprav 300 dle tvého initu)
                    int i = idx(sim.map.width,x,y);
                    int amt  = sim.map.res_amount[i];
                    int maxv = std::max(1, (int)sim.map.res_max[i]); // ochrana proti dělení nulou
                    float f  = std::clamp(amt / float(maxv), 0.2f, 1.0f); // 20–100 % jasu
                    Uint8 mod = (Uint8)(200 * f + 55);

                    SDL_SetTextureColorMod(texRes, mod, mod, mod);
                    SDL_RenderCopy(ren, texRes, &s, &d);
                    SDL_SetTextureColorMod(texRes, 255,255,255);
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
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            for(int r=0; r<4; ++r){
                SDL_Rect sh{ c.x-16+r, c.y-6+r/2, 32-2*r, 10-r/2 };
                SDL_SetRenderDrawColor(ren, 0,0,0, (Uint8)(38 - r*8));
                SDL_RenderFillRect(ren, &sh);
            }
            SDL_Rect src = src_for_unit(u);
            SDL_Rect dst = { c.x-UNIT_W/2, c.y-UNIT_H+ISO_H/2, UNIT_W, UNIT_H };
            SDL_RenderCopy(ren, texUnits, &src, &dst);
            if(u.selected){
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 80,200,120,180);
                SDL_Rect ring{ c.x-18, c.y-4, 36, 8 };
                for(int i=0;i<36;i++){
                    float t0 = (float)i/36.0f * 6.28318f;
                    float t1 = (float)(i+1)/36.0f * 6.28318f;
                    int x0 = ring.x + ring.w/2 + (int)((ring.w/2)*0.5f * std::cos(t0));
                    int y0 = ring.y + ring.h/2 + (int)((ring.h/2)       * std::sin(t0));
                    int x1 = ring.x + ring.w/2 + (int)((ring.w/2)*0.5f * std::cos(t1));
                    int y1 = ring.y + ring.h/2 + (int)((ring.h/2)       * std::sin(t1));
                    SDL_RenderDrawLine(ren, x0,y0, x1,y1);
                }
            }
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
       {
            int mx, my; SDL_GetMouseState(&mx, &my);
            if (over_any_ui(mx,my)) {
                hover = {-1,-1};
            } else {
                int ux = int(mx / g_zoom);
                int uy = int(my / g_zoom);
                hover = screen_px_to_iso(ux, uy);
            }
        }
        if (bmode!=BuildMode::None && hover.x>=0 && hover.y>=0) {
            auto bt = get_btype(
                bmode==BuildMode::Dropoff ? BuildingKind::Dropoff :
                bmode==BuildMode::Farm    ? BuildingKind::Farm    :
                                            BuildingKind::Barracks
            );
            Vec2i t = hover;
            bool ok = (t.x>=0 && t.y>=0 && t.x+bt.w<=sim.map.width && t.y+bt.h<=sim.map.height)
                    && can_place_building(sim, bt, t.x, t.y);
            bool enough = (sim.gold >= bt.cost_gold && sim.wood >= bt.cost_wood);

            {
                const bool placeOk = ok && enough;
                const Uint8 R = placeOk ?  80 : 200;
                const Uint8 G = placeOk ? 200 :  80;
                const Uint8 B = 80;
                SDL_SetRenderDrawColor(ren, R, G, B, 220);
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

                for(int yy=0; yy<bt.h; ++yy){
                    for(int xx=0; xx<bt.w; ++xx){
                        draw_iso_tile_outline(ren, t.x + xx, t.y + yy);
                    }
                }
                // volitelný „crosshair“ do středu footprintu:
                SDL_Point c0 = iso_to_screen_px(t.x,      t.y);
                SDL_Point c2 = iso_to_screen_px(t.x+bt.w, t.y+bt.h);
                int cx = (c0.x + c2.x) / 2;
                int cy = (c0.y + c2.y) / 2;
                SDL_RenderDrawLine(ren, cx-8, cy,   cx+8, cy);
                SDL_RenderDrawLine(ren, cx,   cy-8, cx,   cy+8);
            }

            SDL_Point c = iso_to_screen_px(t.x, t.y);
            SDL_Rect src = src_for_build(bt.kind);
            SDL_Rect dst = { c.x - 32, c.y - 40, 64, 48 };
            if(bt.w==2) dst.y -= 12;

            SDL_SetTextureAlphaMod(texBuild, ok?160:80);
            SDL_SetTextureColorMod(texBuild, ok?80:200, ok?200:80, 80);
            SDL_RenderCopy(ren, texBuild, &src, &dst);
            SDL_SetTextureAlphaMod(texBuild, 255);
            SDL_SetTextureColorMod(texBuild, 255,255,255);

            // tooltip: icony + čísla
            SDL_Rect tip{ c.x+36, c.y-68, 160, 64 };
            clamp_world_rect_to_window(tip, g_zoom, 6);
            SDL_SetRenderDrawColor(ren,20,20,28,230); SDL_RenderFillRect(ren,&tip);
            SDL_SetRenderDrawColor(ren, (ok && enough)? 80:140, (ok && enough)? 200:80, 80, 255);
            SDL_RenderDrawRect(ren,&tip);

            SDL_Rect icG{ tip.x+6, tip.y+8, 16, 16 };
            SDL_Rect srcG = icon_src_32(0); SDL_RenderCopy(ren, texIcons, &srcG, &icG);
            SDL_SetRenderDrawColor(ren,230,230,230,255); draw_number(ren, tip.x+26, tip.y+10, bt.cost_gold);

            SDL_Rect icW{ tip.x+6, tip.y+30, 16, 16 };
            SDL_Rect srcW = icon_src_32(1); SDL_RenderCopy(ren, texIcons, &srcW, &icW);
            SDL_SetRenderDrawColor(ren,230,230,230,255); draw_number(ren, tip.x+26, tip.y+32, bt.cost_wood);

            if (font){
                if (!ok)        draw_text(ren, font, "Blocked / Not grass", tip.x+72, tip.y+10, SDL_Color{230,120,120,255});
                else if(!enough)draw_text(ren, font, "Not enough resources", tip.x+72, tip.y+10, SDL_Color{230,120,120,255});
                else            draw_text(ren, font, "Place: OK",           tip.x+72, tip.y+10, SDL_Color{160,220,160,255});
            }
        }

        // --- Resource popup (draw in world scale) ---
        if(g_resPopup.tile_x >= 0 && g_resPopup.tile_y >= 0 && g_resPopup.kind != (uint8_t)ResourceKind::None){
            uint32_t now = SDL_GetTicks();
            if(now > g_resPopup.until_ms){
                g_resPopup.tile_x = -1;
            } else {
                int i = idx(sim.map.width, g_resPopup.tile_x, g_resPopup.tile_y);
                int amt  = sim.map.res_amount[i];
                int hasMax = !sim.map.res_max.empty();
                int maxv = hasMax ? sim.map.res_max[i] : std::max(amt,1);
                float pct = std::clamp(amt / float(std::max(maxv,1)), 0.f, 1.f);

                SDL_Point p = iso_to_screen_px(g_resPopup.tile_x, g_resPopup.tile_y);
                int px = p.x;
                int py = p.y - 28;

                const char* name = (g_resPopup.kind==(uint8_t)ResourceKind::Gold) ? "Zlato" : "Dřevo";
                char line1[24]; std::snprintf(line1, sizeof(line1), "%s", name);
                char line2[32];
                if(hasMax) std::snprintf(line2, sizeof(line2), "%d / %d", amt, maxv);
                else       std::snprintf(line2, sizeof(line2), "%d", amt);

                // Změna: vypočítáme šířku textu, bublinu uděláme dost širokou
                int t1w=0,t1h=0,t2w=0,t2h=0;
                TTF_SizeUTF8(font, line1, &t1w, &t1h);
                TTF_SizeUTF8(font, line2, &t2w, &t2h);
                int text_w = std::max(t1w, t2w);
                int padX = 8, padY = 6;
                int barH = hasMax ? 6 : 0;
                int w = std::max(120, text_w + padX*2);            // min 120 px, nebo podle textu
                int h = padY + t1h + 2 + t2h + (hasMax ? (4 + barH) : 0) + padY;

                SDL_Rect bg{ px - w/2, py - h, w, h };
                // Anti-clipping: udrž bublinu v okně i při různém zoomu
                clamp_world_rect_to_window(bg, g_zoom, /*margin*/ 4);

                // šipka navázaná na spodní hranu bubliny (ať nezajede mimo)
                int tri_x = px;                 // původní střed, ale omezíme do šířky bg
                int tri_y = bg.y + bg.h;
                if(tri_x < bg.x + 6)            tri_x = bg.x + 6;
                if(tri_x > bg.x + bg.w - 6)     tri_x = bg.x + bg.w - 6;

                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 16, 20, 24, 210); SDL_RenderFillRect(ren, &bg);
                SDL_SetRenderDrawColor(ren, 70, 80, 90, 255); SDL_RenderDrawRect(ren, &bg);

                // Texty: na dva řádky, bar půjde POD ně (=> žádné překrytí)
                int tx = bg.x + padX;
                int ty = bg.y + padY;
                draw_text(ren, font, line1, tx, ty,                 {255,255,255,255});
                draw_text(ren, font, line2, tx, ty + t1h + 2,       {220,220,220,255});

                if(hasMax){
                    SDL_Rect barBg{ bg.x + padX, bg.y + h - padY - barH, w - 2*padX, barH };
                    SDL_SetRenderDrawColor(ren, 50, 54, 60, 255); SDL_RenderFillRect(ren, &barBg);

                    SDL_Rect barFg{ barBg.x, barBg.y, int(barBg.w * pct), barBg.h };
                    if(g_resPopup.kind==(uint8_t)ResourceKind::Gold)
                        SDL_SetRenderDrawColor(ren, 212, 175, 55, 255);
                    else
                        SDL_SetRenderDrawColor(ren, 96, 160, 72, 255);
                    SDL_RenderFillRect(ren, &barFg);
                }

                SDL_Point tri[4] = {
                    { tri_x, tri_y }, { tri_x-5, tri_y-8 }, { tri_x+5, tri_y-8 }, { tri_x, tri_y }
                };
                SDL_SetRenderDrawColor(ren, 70, 80, 90, 255);
                SDL_RenderDrawLines(ren, tri, 4);
            }
        }

        SDL_RenderSetScale(ren, 1.0f, 1.0f);

        // ---------- UI LAYERS ----------
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        for(int y=0; y<WINDOW_H; y+=4){
            Uint8 a = (Uint8)std::min(60 + (y * 80 / WINDOW_H), 120); // 60..120
            SDL_SetRenderDrawColor(ren, 24, 40, 60, a);
            SDL_RenderDrawLine(ren, 0, y, WINDOW_W, y);
        }
        // Topbar
        {
            SDL_Rect bar{0,0,WINDOW_W, UI_TOPBAR_H};
            sdl_fill(ren, bar, 12,14,20,255);
            fake_gradient(ren, bar, SDL_Color{24,28,38,255}, 70, 120);
            bevel_box(ren, bar, SDL_Color{18,20,28,0}, SDL_Color{60,80,110,255}, SDL_Color{120,150,190,180}, 255);

            SDL_Rect icG{12,16,24,24};
            SDL_Rect icW{116,16,24,24};
            SDL_Rect icF{220,16,24,24};
            SDL_Rect srcGold = icon_src_32(0);
            SDL_Rect srcWood = icon_src_32(1);
            SDL_Rect srcFarm = icon_src_32(3);
            SDL_RenderCopy(ren, texIcons, &srcGold, &icG);
            SDL_RenderCopy(ren, texIcons, &srcWood, &icW);
            SDL_RenderCopy(ren, texIcons, &srcFarm, &icF);

            SDL_Color txt{235,235,240,255};
            if(font){
                draw_text(ren, font, std::to_string(sim.gold).c_str(), icG.x+28, 22, txt);
                draw_text(ren, font, std::to_string(sim.wood).c_str(), icW.x+28, 22, txt);
                char foodBuf[32]; std::snprintf(foodBuf, sizeof(foodBuf), "%d/%d", sim.food_used, sim.food_cap);
                draw_text(ren, font, foodBuf, icF.x+28, 22, txt);
            }

            // supply bar (ala WC)
            SDL_Rect supBg{ 320, 22, 170, 16 };
            float t = (sim.food_cap>0) ? (float)sim.food_used/(float)sim.food_cap : 0.f;
            fill_bar(ren, supBg, t, SDL_Color{40,44,54,255}, SDL_Color{100,200,120,255});
            bevel_box(ren, supBg, SDL_Color{0,0,0,0}, SDL_Color{60,80,110,255}, SDL_Color{120,150,190,180}, 255);
        }

        // Selection panel
        {
            SDL_Rect rc = selPanel;
            // podklad + jemný gradient + bevel rám
            sdl_fill(ren, rc, 12,14,20,255);
            fake_gradient(ren, rc, SDL_Color{22,26,34,255}, 60, 110);
            bevel_box(ren, rc, SDL_Color{0,0,0,0}, SDL_Color{60,80,110,255}, SDL_Color{120,150,190,160}, 255);

            int selUnits = 0; for (auto& u: sim.units) if (u.selected) selUnits++;
            BuildingId sbid = get_selected_building_id(sim);

            if (sbid){
                const Building* b = find_building(sim, sbid);
                if (b){
                    // levý box s náhledem budovy (taky bevel)
                    SDL_Rect box{ selPanel.x+8, selPanel.y+8, 120, 90 };
                    sdl_fill(ren, box, 18,20,28,255);
                    fake_gradient(ren, box, SDL_Color{26,30,40,255}, 60, 110);
                    bevel_box(ren, box, SDL_Color{0,0,0,0}, SDL_Color{60,80,110,255}, SDL_Color{120,150,190,150}, 255);

                    SDL_Rect src = src_for_build(b->kind);
                    SDL_Rect dst{ box.x+(box.w-64)/2, box.y+10, 64, 48 };
                    SDL_RenderCopy(ren, texBuild, &src, &dst);

                    // progress bar stavby (bevel + výplň)
                    if (b->state != BuildState::Complete){
                        SDL_Rect pbg{ box.x+12, box.y+68, 96, 6 };
                        float rt = b->build_total_ms ? (float)b->build_progress_ms / (float)b->build_total_ms : 0.f;
                        fill_bar(ren, pbg, rt, SDL_Color{40,44,54,255}, SDL_Color{100,200,120,255});
                        bevel_box(ren, pbg, SDL_Color{0,0,0,0}, SDL_Color{60,80,110,255}, SDL_Color{120,150,190,160}, 255);
                    }

                    // sem si můžeš dopsat text info (název budovy atd.), máš-li font:
                    if (font){
                        const char* name =
                            (b->kind==BuildingKind::Dropoff) ? "Drop-off" :
                            (b->kind==BuildingKind::Farm)    ? "Farm"     :
                                                            "Barracks";
                        draw_text(ren, font, name, box.x+8, box.y + box.h - 18, SDL_Color{210,210,210,255});
                    }
                }
            } else {
                // žádná budova vybraná: jen počet vybraných jednotek
                SDL_Color txt{230,230,230,255};
                if (font) {
                    char buf[32]; std::snprintf(buf, sizeof(buf), "Selected: %d", selUnits);
                    draw_text(ren, font, buf, selPanel.x+12, selPanel.y+12, txt);
                } else {
                    SDL_SetRenderDrawColor(ren,230,230,230,255);
                    draw_number(ren, selPanel.x+12, selPanel.y+12, selUnits);
                }
            }
        }

        // Command card
        {
            SDL_Rect rc = cmdCard;
            // podklad + jemný gradient + bevel rám
            sdl_fill(ren, rc, 12,14,20,255);
            fake_gradient(ren, rc, SDL_Color{22,26,34,255}, 60, 110);
            bevel_box(ren, rc, SDL_Color{0,0,0,0}, SDL_Color{60,80,110,255}, SDL_Color{120,150,190,160}, 255);

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

                if(font){
                    draw_text(ren, font, "Drop-off", cardBtns[0].x+8, cardBtns[0].y + cardBtns[0].h - 16, SDL_Color{210,210,220,255});
                    draw_text(ren, font, "Farm",     cardBtns[1].x+8, cardBtns[1].y + cardBtns[1].h - 16, SDL_Color{210,210,220,255});
                    draw_text(ren, font, "Barracks", cardBtns[2].x+8, cardBtns[2].y + cardBtns[2].h - 16, SDL_Color{210,210,220,255});
                }

                int mx,my; SDL_GetMouseState(&mx,&my);
                // hover efekt
                cc_btn_hover(ren, b0.r, point_in(b0.r,mx,my));
                cc_btn_hover(ren, b1.r, point_in(b1.r,mx,my));
                cc_btn_hover(ren, b2.r, point_in(b2.r,mx,my));

                // badge s hotkeyem
                cc_badge(ren, font, b0.r, "1");
                cc_badge(ren, font, b1.r, "2");
                cc_badge(ren, font, b2.r, "3");

                if(point_in(b0.r, mx,my)) draw_build_tooltip(ren, font, mx,my, b0.label,b0.hotkey,b0.costGold,b0.costWood,b0.enabled);
                if(point_in(b1.r, mx,my)) draw_build_tooltip(ren, font, mx,my, b1.label,b1.hotkey,b1.costGold,b1.costWood,b1.enabled);
                if(point_in(b2.r, mx,my)) draw_build_tooltip(ren, font, mx,my, b2.label,b2.hotkey,b2.costGold,b2.costWood,b2.enabled);

            } else if(sbid){
                const Building* b = find_building(sim, sbid);
                if(b && b->kind==BuildingKind::Barracks && b->state==BuildState::Complete){
                    bool canFoot = (sim.gold >= 100 && sim.food_used+1 <= sim.food_cap);
                    Btn bf{cardBtns[0], 5, canFoot, "Footman", "Q", 100, 0};
                    Btn bp{cardBtns[1], 7, true,    "x5",      "W", 0, 0};
                    Btn bc{cardBtns[2], 6, true,    "Cancel",  "E", 0, 0};
                    draw_cc_btn(ren, texIcons, bf);
                    draw_cc_btn(ren, texIcons, bp);
                    draw_cc_btn(ren, texIcons, bc);

                    if(font){
                        draw_text(ren, font, "Footman", cardBtns[0].x+8, cardBtns[0].y + cardBtns[0].h - 16, SDL_Color{210,210,220,255});
                        draw_text(ren, font, "x5",      cardBtns[1].x+8, cardBtns[1].y + cardBtns[1].h - 16, SDL_Color{210,210,220,255});
                        draw_text(ren, font, "Cancel",  cardBtns[2].x+8, cardBtns[2].y + cardBtns[2].h - 16, SDL_Color{210,210,220,255});
                    }

                    int mx,my; SDL_GetMouseState(&mx,&my);
                    // hover
                    cc_btn_hover(ren, bf.r, point_in(bf.r,mx,my));
                    cc_btn_hover(ren, bp.r, point_in(bp.r,mx,my));
                    cc_btn_hover(ren, bc.r, point_in(bc.r,mx,my));

                    // badge
                    cc_badge(ren, font, bf.r, "Q");
                    cc_badge(ren, font, bp.r, "W");
                    cc_badge(ren, font, bc.r, "E");

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

        // --- Resource info card (UI) ---
        if(g_resInfo.index >= 0 && g_resInfo.kind != (uint8_t)ResourceKind::None){
            uint32_t now = SDL_GetTicks();
            if(now > g_resInfo.until_ms){
                // vypršelo zobrazení
                g_resInfo.index = -1;
            } else {
                // zjisti live hodnoty
                int i = g_resInfo.index;
                // ochrana když mezitím „ujedeme“ jinam (resize mapy by to změnil)
                if(i >= 0 && i < (int)sim.map.res_amount.size()){
                    int amt  = sim.map.res_amount[i];
                    int maxv = 0;
                    if(!sim.map.res_max.empty()){
                        maxv = sim.map.res_max[i];
                    } else {
                        // fallback: když není res_max, ber max = aktuální hodnota (nebude progress bar, jen číslo)
                        maxv = std::max(amt, 1);
                    }
                    float pct = std::clamp(maxv > 0 ? (amt / float(maxv)) : 0.f, 0.f, 1.f);

                    // rozměry a pozice karty: levá část command panelu
                    const int card_w = 240;
                    const int card_h = 56;
                    SDL_Rect card { cmdCard.x + 12, cmdCard.y + 12, card_w, card_h };

                    // Anti-clipping do okna
                    clamp_ui_rect_to_window(card, /*margin*/ 4);

                    // Zarovnat dovnitř cmdCard (nepřetékat mimo panel)
                    if(card.x < cmdCard.x + 4) card.x = cmdCard.x + 4;
                    if(card.y < cmdCard.y + 4) card.y = cmdCard.y + 4;
                    int right_lim = cmdCard.x + cmdCard.w - 4;
                    int bot_lim   = cmdCard.y + cmdCard.h - 4;
                    if(card.x + card.w > right_lim) card.x = right_lim - card.w;
                    if(card.y + card.h > bot_lim)   card.y = bot_lim   - card.h;

                    // pozadí karty
                    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(ren, 20, 24, 28, 230);  SDL_RenderFillRect(ren, &card);
                    SDL_SetRenderDrawColor(ren, 70, 80, 90, 255);  SDL_RenderDrawRect(ren, &card);

                    // ikona vlevo (zlatá nebo dřevěná „tečka“)
                    SDL_Rect icon { card.x + 10, card.y + 10, 24, 24 };
                    if(g_resInfo.kind == (uint8_t)ResourceKind::Gold){
                        SDL_SetRenderDrawColor(ren, 212, 175, 55, 255);
                    } else {
                        SDL_SetRenderDrawColor(ren, 96, 160, 72, 255);
                    }
                    SDL_RenderFillRect(ren, &icon);

                    // text: název + čísla
                    const char* name = (g_resInfo.kind==(uint8_t)ResourceKind::Gold) ? "Zlato" : "Dřevo";
                    char line1[64]; std::snprintf(line1, sizeof(line1), "%s", name);
                    char line2[64];
                    if(!sim.map.res_max.empty()){
                        std::snprintf(line2, sizeof(line2), "%d / %d", amt, maxv);
                    } else {
                        std::snprintf(line2, sizeof(line2), "%d zbývá", amt);
                    }
                    // názvy (použij tvůj draw_text)
                    draw_text(ren, font, line1, card.x + 44, card.y + 8, {255,255,255,255});
                    draw_text(ren, font, line2, card.x + 44, card.y + 28, {220,220,220,255});

                    // progress bar (jen pokud máme max)
                    if(!sim.map.res_max.empty()){
                        SDL_Rect barBg { card.x + 12, card.y + card_h - 14, card_w - 24, 6 };
                        SDL_SetRenderDrawColor(ren, 50, 54, 60, 255); SDL_RenderFillRect(ren, &barBg);
                        SDL_Rect barFg { barBg.x, barBg.y, int(barBg.w * pct), barBg.h };
                        // barva podle typu
                        if(g_resInfo.kind == (uint8_t)ResourceKind::Gold)
                            SDL_SetRenderDrawColor(ren, 212, 175, 55, 255);
                        else
                            SDL_SetRenderDrawColor(ren, 96, 160, 72, 255);
                        SDL_RenderFillRect(ren, &barFg);
                    }
                } else {
                    // mimo rozsah => schovej
                    g_resInfo.index = -1;
                }
            }
        }

        // Queue panel (Warcraft style)
        {
            SDL_Rect rc = queuePanel;
            // panel podklad + gradient + bevel rám
            sdl_fill(ren, rc, 12,14,20,255);
            fake_gradient(ren, rc, SDL_Color{22,26,34,255}, 60, 110);
            bevel_box(ren, rc, SDL_Color{0,0,0,0}, SDL_Color{60,80,110,255}, SDL_Color{120,150,190,160}, 255);

            BuildingId sbid = get_selected_building_id(sim);
            if(sbid){
                const Building* b = find_building(sim, sbid);
                if(b && b->kind==BuildingKind::Barracks){
                    int qx = rc.x + 8;
                    int qy = rc.y + 6;

                    for(size_t i=0; i<b->queue.size() && i<10; ++i){
                        SDL_Rect box{ qx + int(i)*28, qy, 24, 24 };

                        // slot vzhled (bevel + jemný gradient)
                        sdl_fill(ren, box, 18,20,28,255);
                        fake_gradient(ren, box, SDL_Color{26,30,40,255}, 70, 120);
                        bevel_box(ren, box, SDL_Color{0,0,0,0}, SDL_Color{60,80,110,255}, SDL_Color{120,150,190,150}, 255);

                        // ikonka jednotky (footman head = index 5)
                        SDL_Rect idst{ box.x+4, box.y+4, 16, 16 };
                        SDL_Rect isrc = icon_src_32(5);
                        SDL_RenderCopy(ren, texIcons, &isrc, &idst);

                        // (volitelně) zvýrazni první položku ve frontě
                        if(i == 0){
                            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                            SDL_SetRenderDrawColor(ren, 200, 180, 90, 140);
                            SDL_RenderDrawRect(ren, &box);
                        }
                    }
                }
            }
        }

        // --- Drag selection rectangle ---
        if(draggingLocal){
            SDL_Rect r = dragRectLocal;
            if(r.w<0){ r.x+=r.w; r.w=-r.w; }
            if(r.h<0){ r.y+=r.h; r.h=-r.h; }

            // omez na herní viewport (mimo UI panely)
            SDL_Rect vp = world_viewport();
            if(r.x < vp.x){ r.w -= (vp.x - r.x); r.x = vp.x; }
            if(r.y < vp.y){ r.h -= (vp.y - r.y); r.y = vp.y; }
            if(r.x + r.w > vp.x + vp.w) r.w = vp.x + vp.w - r.x;
            if(r.y + r.h > vp.y + vp.h) r.h = vp.y + vp.h - r.y;

            if(r.w > 0 && r.h > 0){
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 120,160,220,60);
                SDL_RenderFillRect(ren, &r);
                SDL_SetRenderDrawColor(ren, 120,160,220,180);
                SDL_RenderDrawRect(ren, &r);
            }
        }

        // Minimap
        {
            SDL_Rect mm_bg{mm_x-2, mm_y-2, mm_w+4, mm_h+4};
            SDL_SetRenderDrawColor(ren, 8, 8, 12, 220); SDL_RenderFillRect(ren, &mm_bg);
            for(int y=0;y<sim.map.height;++y){
                for(int x=0;x<sim.map.width;++x){
                    uint8_t t = sim.map.tiles[idx(sim.map.width,x,y)];
                    Uint8 r=40,g=90,b=40;          // grass
                    if(t==1){ r=90; g=90; b=110; } // rock
                    else if(t==2){ r=190; g=160; b=55; } // sand
                    else if(t==3){ r=40; g=140; b=70; }  // forest
                    else if(t==4){ r=140; g=95;  b=55; } // dirt
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

        {
            uint32_t now = SDL_GetTicks();
            if (now >= next_autosave_ms) {
                if (save_game(sim, "autosave.txt")) {
                    std::printf("[AUTOSAVE] -> autosave.txt\n");
                    hud_show(hud, "Autosaved", 1200);
                } else {
                    std::printf("[AUTOSAVE] FAILED\n");
                    hud_show(hud, "Autosave failed", 1200);
                }
                next_autosave_ms = now + autosave_period_ms;
            }
        }

        {
            uint32_t now = SDL_GetTicks();
            uint8_t a = hud_alpha(hud, now);
            if(a > 0){
                int bw = 260, bh = 28;
                SDL_Rect bar{ (WINDOW_W - bw)/2, 12, bw, bh };
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 20,24,32,a); SDL_RenderFillRect(ren, &bar);
                SDL_SetRenderDrawColor(ren, 90,120,180, 255); SDL_RenderDrawRect(ren, &bar);
                if(font) draw_text(ren, font, hud.text.c_str(), bar.x + 12, bar.y + 6, SDL_Color{230,230,230,255});
            }
        }

        SDL_RenderPresent(ren);

        SDL_Delay(1);
    }

    SDL_DestroyTexture(texTiles); SDL_DestroyTexture(texUnits); SDL_DestroyTexture(texEdges); SDL_DestroyTexture(texBuild); SDL_DestroyTexture(texIcons); SDL_DestroyTexture(texRes); 

    if (font) TTF_CloseFont(font);
        TTF_Quit();

    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    if (sfxSave) Mix_FreeChunk(sfxSave);
    if (sfxLoad) Mix_FreeChunk(sfxLoad);
    Mix_CloseAudio();
    SDL_Quit();
    return 0;
}
