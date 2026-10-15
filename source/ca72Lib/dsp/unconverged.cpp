#include "unconverged.h"

namespace ca72::unconverged
{
std::vector<std::pair<const char*, uint64_t>> take()
{
    std::vector<std::pair<const char*, uint64_t>> out;
    for (size_t i = 0; i < N; ++i)
    {
        const uint64_t n = COUNTS[i];
        COUNTS[i] = 0;
        if (n > 0)
            out.emplace_back (NAMES[i], n);
    }
    return out;
}
} // namespace ca72::unconverged
