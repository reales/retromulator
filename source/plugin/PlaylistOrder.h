#pragma once

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

namespace retromulator
{
    // The order a playlist is played in: its own order, or a shuffled one that starts at
    // the entry that was playing. Positions wrap around.
    class PlaylistOrder
    {
    public:
        // current < 0 with shuffle on starts at a random entry.
        void reset(const int count, const int current, const bool shuffle)
        {
            m_order.resize(static_cast<size_t>(std::max(0, count)));
            std::iota(m_order.begin(), m_order.end(), 0);
            m_shuffle = shuffle;

            if(!shuffle || m_order.size() < 2)
                return;

            static std::mt19937 rng{std::random_device{}()};
            std::shuffle(m_order.begin(), m_order.end(), rng);

            const auto it = std::find(m_order.begin(), m_order.end(), current);
            if(it != m_order.end())
                std::iter_swap(m_order.begin(), it);
        }

        bool isShuffled() const {  return m_shuffle; }
        int size() const {  return static_cast<int>(m_order.size()); }

        int positionOf(const int index) const
        {
            const auto it = std::find(m_order.begin(), m_order.end(), index);
            return it == m_order.end() ? 0 : static_cast<int>(it - m_order.begin());
        }

        int indexAt(const int position) const
        {
            const int count = size();
            if(count == 0)
                return 0;
            return m_order[static_cast<size_t>(((position % count) + count) % count)];
        }

        bool isLast(const int index) const { return positionOf(index) >= size() - 1; }

    private:
        std::vector<int> m_order;
        bool m_shuffle = false;
    };
}
