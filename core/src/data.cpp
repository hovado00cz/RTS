
#include "data.hpp"
#include "sim.hpp"
#include "types.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>

bool load_units_csv(const std::string& path, std::vector<UnitType>& out, std::unordered_map<std::string,uint16_t>& index){
    std::ifstream f(path);
    if(!f.good()){ std::fprintf(stderr,"Failed to open %s\n", path.c_str()); return false; }
    std::string line; std::getline(f,line);
    while(std::getline(f,line)){
        if(line.empty()) continue;
        std::stringstream ss(line); std::string tok; UnitType t; Attack a; Armor ar;
        std::getline(ss,t.id,',');
        std::getline(ss,tok,','); t.hp=std::stoi(tok);
        std::getline(ss,tok,','); t.move_speed_px_s=std::stoi(tok);
        std::getline(ss,tok,','); a.type=(tok=="Pierce"?AttackType::Pierce: tok=="Siege"?AttackType::Siege: tok=="Magic"?AttackType::Magic: AttackType::Normal);
        std::getline(ss,tok,','); a.damage=(int16_t)std::stoi(tok);
        std::getline(ss,tok,','); a.cooldown_ms=(uint16_t)std::stoi(tok);
        std::getline(ss,tok,','); a.range_tiles=(uint16_t)::std::stoi(tok);
        std::getline(ss,tok,','); ar.type=(tok=="Light"?ArmorType::Light: tok=="Heavy"?ArmorType::Heavy: tok=="Building"?ArmorType::Building: ArmorType::Medium);
        std::getline(ss,tok,','); ar.value=(int16_t)std::stoi(tok);
        std::getline(ss,tok,','); t.sight_tiles=std::stoi(tok);
        std::getline(ss,tok,','); t.cost_gold=std::stoi(tok);
        std::getline(ss,tok,','); t.cost_wood=std::stoi(tok);
        std::getline(ss,tok,','); t.food=std::stoi(tok);
        std::getline(ss,tok,','); t.build_time_ms=std::stoi(tok);
        t.attack=a; t.armor=ar;
        index[t.id]=(uint16_t)out.size();
        out.push_back(t);
    }
    return true;
}

bool load_map_txt(const std::string& path, Map& out){
    std::ifstream f(path);
    if(!f.good()) return false;
    std::string line; std::vector<std::string> lines;
    while(std::getline(f,line)){ if(!line.empty()) lines.push_back(line); }
    if(lines.empty()) return false;
    out.height=(int)lines.size(); out.width=(int)lines[0].size();
    out.tiles.assign(out.width*out.height,0);
    out.blocked.assign(out.width*out.height,0);
    for(int y=0;y<out.height;++y){
        for(int x=0;x<out.width;++x){
            char c=lines[y][x]; uint8_t v=0;
            if(c=='#') v=1;
            else if(c=='G') v=2;
            else if(c=='T') v=3;
            else if(c=='D') v=4;
            else v=0;
            out.tiles[y*out.width+x]=v;
        }
    }
    return true;
}
