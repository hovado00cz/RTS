
#pragma once
#include <cstdint>
struct RNG{ uint32_t s; explicit RNG(uint32_t seed=1):s(seed?seed:1){}
uint32_t next(){ uint32_t x=s; x^=x<<13; x^=x>>17; x^=x<<5; s=x; return x;}
uint32_t next_range(uint32_t n){ return n?next()%n:0;} };
