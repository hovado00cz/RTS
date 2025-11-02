
#include "pathfinding.hpp"
#include "sim.hpp"
#include <queue>
#include <vector>
#include <limits>
#include <cmath>
#include <algorithm>

static inline int idx(int w,int x,int y){ return y*w+x; }
static inline int hcost(Vec2i a, Vec2i b){ int dx=std::abs(a.x-b.x), dy=std::abs(a.y-b.y); return (dx+dy)*10; }

bool astar_find(const Map& map, Vec2i start, Vec2i goal, std::vector<Vec2i>& out_path, bool diag, int max_nodes){
    if(start.x==goal.x && start.y==goal.y){ out_path.clear(); return true; }
    const int W=map.width, H=map.height;
    std::vector<int> g(W*H, std::numeric_limits<int>::max());
    std::vector<int> parent(W*H, -1);
    struct Node{ int f,x,y; };
    auto cmp=[](const Node& a,const Node& b){ return a.f>b.f; };
    std::priority_queue<Node,std::vector<Node>,decltype(cmp)> open(cmp);
    int sidx=idx(W,start.x,start.y), gidx=idx(W,goal.x,goal.y);
    g[sidx]=0; open.push({hcost(start,goal), start.x, start.y});
    int processed=0;
    auto is_block=[&](int x,int y){
        if(x<0||y<0||x>=W||y>=H) return true;
        uint8_t t = map.tiles[idx(W,x,y)];
        if(map.blocked[idx(W,x,y)]) return true;
        return t==1; // walls block only
    };
    const int dirs8[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    const int dirs4[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    auto dirs = diag?dirs8:dirs4;
    int dcount = diag?8:4;
    while(!open.empty()){
        Node n = open.top(); open.pop();
        if(++processed>max_nodes) break;
        if(n.x==goal.x && n.y==goal.y) break;
        int ni = idx(W,n.x,n.y);
        for(int i=0;i<dcount;++i){
            int nx=n.x+dirs[i][0], ny=n.y+dirs[i][1];
            if(is_block(nx,ny)) continue;
            int cost = g[ni] + ((i<4)?10:14);
            int nidx = idx(W,nx,ny);
            if(cost < g[nidx]){
                g[nidx]=cost;
                parent[nidx]=ni;
                int f = cost + hcost({nx,ny}, goal);
                open.push({f,nx,ny});
            }
        }
    }
    if(parent[gidx]==-1) return false;
    out_path.clear();
    int cur=gidx;
    while(cur!=sidx && cur!=-1){
        int y=cur/W, x=cur%W;
        out_path.push_back({x,y});
        cur=parent[cur];
    }
    std::reverse(out_path.begin(), out_path.end());
    return true;
}
