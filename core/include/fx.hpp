
#pragma once
#include <cstdint>
#include <cmath>
struct fx{ int32_t v; constexpr fx():v(0){} explicit constexpr fx(int32_t r):v(r){} 
static fx from_float(float f){ return fx{ (int32_t)llroundf(f*65536.0f)}; } 
float to_float() const { return (float)v/65536.0f; } };
inline fx operator+(fx a, fx b){ return fx{a.v+b.v}; }
inline fx operator-(fx a, fx b){ return fx{a.v-b.v}; }
inline fx operator*(fx a, fx b){ return fx{ (int32_t)(((int64_t)a.v*(int64_t)b.v)>>16)}; }
inline fx operator/(fx a, fx b){ return fx{ (int32_t)(((int64_t)a.v<<16)/(int64_t)b.v)}; }
inline fx fx_from_int(int i){ return fx{i<<16}; }
inline int fx_floor_to_int(fx a){ return a.v>>16; }
