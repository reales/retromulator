#pragma once

#include "common.h"

#include <mutex>

namespace ca72::shared
{
template <typename K, typename T>
struct Shared
{
    std::mutex made_mutex;
    std::vector<std::pair<K, std::shared_ptr<const T>>> made;

    static Shared new_() { return Shared {}; }
    static Shared default_() { return new_(); }

    template <typename F>
    std::shared_ptr<const T> get (K key, F make)
    {
        std::lock_guard<std::mutex> lock (made_mutex);
        for (const auto& [k, t] : made)
            if (k == key)
                return t;
        std::shared_ptr<const T> t = std::make_shared<const T> (make());
        made.emplace_back (std::move (key), t);
        return t;
    }
};
} // namespace ca72::shared
