
#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
struct UnitType; struct Map;
bool load_units_csv(const std::string& path,
                    std::vector<UnitType>& out,
                    std::unordered_map<std::string,uint16_t>& index);
bool load_map_txt(const std::string& path, Map& out);
