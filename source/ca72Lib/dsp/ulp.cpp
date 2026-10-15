#include "ulp.h"

namespace ca72::ulp
{
std::vector<std::pair<std::string, uint64_t>> calls()
{
    std::vector<std::pair<std::string, uint64_t>> v;
    std::stable_sort (v.begin(), v.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
    return v;
}
} // namespace ca72::ulp
