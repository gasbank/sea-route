#pragma once

#include "xy.hpp"

namespace astarrtree {
    namespace bi = boost::interprocess;
    namespace bg = boost::geometry;
    namespace bgm = bg::model;
    namespace bgi = bg::index;

    typedef bgm::point<int, 2, bg::cs::cartesian> point_t;
    typedef bgm::box<point_t> box_t;
    typedef std::pair<box_t, int> value_t;
    typedef bgi::linear<32, 8> params_t;
    typedef bgi::indexable<value_t> indexable_t;
    typedef bgi::equal_to<value_t> equal_to_t;
    typedef bi::allocator<value_t, bi::managed_mapped_file::segment_manager> allocator_t;
    typedef bgi::rtree<value_t, params_t, indexable_t, equal_to_t, allocator_t> rtree_t;

    void astar_rtree(const char* output, size_t output_max_size, xy32 from, xy32 to);
    std::vector<xy32> astar_rtree_memory(rtree_t* rtree_ptr, xy32 from, xy32 to);
}
