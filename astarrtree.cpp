#include "precompiled.hpp"
#include "astarrtree.hpp"
#include "AStar.h"

namespace bi = boost::interprocess;
namespace bg = boost::geometry;
namespace bgm = bg::model;
namespace bgi = bg::index;

typedef bgm::point<short, 2, bg::cs::cartesian> point_t;
typedef bgm::box<point_t> box_t;
typedef std::pair<box_t, int> value_t;
typedef bgi::linear<32, 8> params_t;
typedef bgi::indexable<value_t> indexable_t;
typedef bgi::equal_to<value_t> equal_to_t;
typedef bi::allocator<value_t, bi::managed_mapped_file::segment_manager> allocator_t;
typedef bgi::rtree<value_t, params_t, indexable_t, equal_to_t, allocator_t> rtree_t;

xyxy xyxy_from_box_t(const box_t& v) {
    xyxy r;
    r.xy0.x = v.min_corner().get<0>();
    r.xy0.y = v.min_corner().get<1>();
    r.xy1.x = v.max_corner().get<0>();
    r.xy1.y = v.max_corner().get<1>();
    return r;
}

box_t box_t_from_xyxy(const xyxy& v) {
    box_t r;
    r.min_corner().set<0>(v.xy0.x);
    r.min_corner().set<1>(v.xy0.y);
    r.max_corner().set<0>(v.xy1.x);
    r.max_corner().set<1>(v.xy1.y);
    return r;
}

void RTreePathNodeNeighbors(ASNeighborList neighbors, void *node, void *context) {
    xyxy* n = reinterpret_cast<xyxy*>(node);
    rtree_t* rtree_ptr = reinterpret_cast<rtree_t*>(context);
    box_t query_box = box_t_from_xyxy(*n);
    std::vector<value_t> result_s;
    rtree_ptr->query(bgi::intersects(query_box), std::back_inserter(result_s));
    for (const auto& v : result_s) {
        auto n2 = xyxy_from_box_t(v.first);
        ASNeighborListAdd(neighbors, &n2, 1);
    }
}

float RTreePathNodeHeuristic(void *fromNode, void *toNode, void *context) {
    xyxy* from = reinterpret_cast<xyxy*>(fromNode);
    xyxy* to = reinterpret_cast<xyxy*>(toNode);
    float fromMedX = (from->xy1.x - from->xy0.x) / 2.0f;
    float fromMedY = (from->xy1.y - from->xy0.y) / 2.0f;
    float toMedX = (to->xy1.x - to->xy0.x) / 2.0f;
    float toMedY = (to->xy1.y - to->xy0.y) / 2.0f;
    return fabsf(fromMedX - toMedX) + fabsf(fromMedY - toMedY);
}

int RTreePathNodeComparator(void *node1, void *node2, void *context) {
    xyxy* n1 = reinterpret_cast<xyxy*>(node1);
    int n1v = n1->xy0.y << 16 | n1->xy0.x;
    xyxy* n2 = reinterpret_cast<xyxy*>(node2);
    int n2v = n2->xy0.y << 16 | n2->xy0.x;
    int d = n1v - n2v;
    if (d == 0) {
        return 0;
    } else if (d > 0) {
        return 1;
    } else {
        return -1;
    }
}


float xyi_distance(const xyi& a, const xyi& b) {
    int dx = a.p.x - b.p.x;
    int dy = a.p.y - b.p.y;
    return static_cast<float>(abs(dx) + abs(dy)); // sqrtf(static_cast<float>(dx * dx + dy * dy));
}

struct pixel_waypoint_search {
    xy from;
    xy to;
    ASPath cell_path;
};

void AddNeighborWithLog(ASNeighborList neighbors, xyi* n, int x, int y, size_t i) {
    xyi neighbor = { { x, y }, i };
    ASNeighborListAdd(neighbors, &neighbor, xyi_distance(*n, neighbor));
    /*printf("Neighbor of (%5d,%5d)[%5d] : (%5d,%5d)[%5d]\n",
    n->p.x,
    n->p.y,
    n->i,
    x,
    y,
    i);*/
}

void RTreePixelPathNodeNeighbors(ASNeighborList neighbors, void *node, void *context) {
    xyi* n = reinterpret_cast<xyi*>(node);
    pixel_waypoint_search* pws = reinterpret_cast<pixel_waypoint_search*>(context);
    ASPath cell_path = pws->cell_path;
    size_t cell_path_count = ASPathGetCount(cell_path);
    if (n->p.x == pws->to.x && n->p.y == pws->to.y) {
        // 'n' equals to 'to': reached endpoint (to-pixel)
        // no neighbors on endpoint
        return;
    } else if (n->i == cell_path_count - 1) {
        // all last cells' pixels (except endpoint pixel) always have the only neighbor: endpoint pixel
        AddNeighborWithLog(neighbors, n, pws->to.x, pws->to.y, cell_path_count - 1);
        return;
    }
    xyxy* n1c = reinterpret_cast<xyxy*>(ASPathGetNode(cell_path, n->i + 0)); // Cell node containing 'n'
    xyxy* n2c = reinterpret_cast<xyxy*>(ASPathGetNode(cell_path, n->i + 1)); // Next Cell node
    bool d = false, u = false, r = false, l = false;
    if (n1c->xy1.y == n2c->xy0.y) {
        // [D]OWN
        //  n1c
        // -----
        //  n2c
        d = true;
    }
    if (n2c->xy1.y == n1c->xy0.y) {
        // [U]P
        //  n2c
        // -----
        //  n1c
        u = true;
    }
    if (n1c->xy1.x == n2c->xy0.x) {
        // [R]IGHT
        //      |
        //  n1c | n2c
        //      |
        r = true;
    }
    if (n2c->xy1.x == n1c->xy0.x) {
        // [L]EFT
        //      |
        //  n2c | n1c
        //      |
        l = true;
    }
    if (d && !u && r && !l) {
        // DOWN & RIGHT corner
        AddNeighborWithLog(neighbors, n, n2c->xy0.x, n2c->xy0.y, n->i + 1);
        //xyi corner_neighbor = { n2c->xy0, n->i + 1 };
        //ASNeighborListAdd(neighbors, &corner_neighbor, static_cast<float>(xyi_distance_sq(*n, corner_neighbor)));
    } else if (!d && u && r && !l) {
        // UP & RIGHT corner
        AddNeighborWithLog(neighbors, n, n2c->xy0.x, n2c->xy1.y - 1, n->i + 1);
        //xyi corner_neighbor = { n2c->xy0, n->i + 1 };
        //corner_neighbor.p.y = n2c->xy1.y - 1;
        //ASNeighborListAdd(neighbors, &corner_neighbor, static_cast<float>(xyi_distance_sq(*n, corner_neighbor)));
    } else if (!d && u && !r && l) {
        // UP & LEFT corner
        AddNeighborWithLog(neighbors, n, n2c->xy1.x - 1, n2c->xy1.y - 1, n->i + 1);
        //xyi corner_neighbor = { n2c->xy1, n->i + 1 };
        //corner_neighbor.p.x--;
        //corner_neighbor.p.y--;
        //ASNeighborListAdd(neighbors, &corner_neighbor, static_cast<float>(xyi_distance_sq(*n, corner_neighbor)));
    } else if (d && !u && !r && l) {
        // DOWN & LEFT corner
        AddNeighborWithLog(neighbors, n, n2c->xy1.x - 1, n2c->xy0.y, n->i + 1);
        //xyi corner_neighbor = { n2c->xy0, n->i + 1 };
        //corner_neighbor.p.x = n2c->xy1.x - 1;
        //ASNeighborListAdd(neighbors, &corner_neighbor, static_cast<float>(xyi_distance_sq(*n, corner_neighbor)));
    } else if (d && !u && !r && !l) {
        // DOWN line
        for (int x = std::max(n1c->xy0.x, n2c->xy0.x); x < std::min(n1c->xy1.x, n2c->xy1.x); x++) {
            AddNeighborWithLog(neighbors, n, x, n2c->xy0.y, n->i + 1);
            //xyi line_neighbor = { { x, n2c->xy0.y }, n->i + 1 };
            //ASNeighborListAdd(neighbors, &line_neighbor, static_cast<float>(xyi_distance_sq(*n, line_neighbor)));
        }
    } else if (!d && u && !r && !l) {
        // UP line
        for (int x = std::max(n1c->xy0.x, n2c->xy0.x); x < std::min(n1c->xy1.x, n2c->xy1.x); x++) {
            AddNeighborWithLog(neighbors, n, x, n2c->xy1.y - 1, n->i + 1);
            //xyi line_neighbor = { { x, n2c->xy1.y - 1 }, n->i + 1 };
            //ASNeighborListAdd(neighbors, &line_neighbor, static_cast<float>(xyi_distance_sq(*n, line_neighbor)));
        }
    } else if (!d && !u && r && !l) {
        // RIGHT line
        for (int y = std::max(n1c->xy0.y, n2c->xy0.y); y < std::min(n1c->xy1.y, n2c->xy1.y); y++) {
            AddNeighborWithLog(neighbors, n, n2c->xy0.x, y, n->i + 1);
            //xyi line_neighbor = { { n2c->xy0.x, y }, n->i + 1 };
            //ASNeighborListAdd(neighbors, &line_neighbor, static_cast<float>(xyi_distance_sq(*n, line_neighbor)));
        }
    } else if (!d && !u && !r && l) {
        // LEFT line
        for (int y = std::max(n1c->xy0.y, n2c->xy0.y); y < std::min(n1c->xy1.y, n2c->xy1.y); y++) {
            AddNeighborWithLog(neighbors, n, n2c->xy1.x - 1, y, n->i + 1);
            //xyi line_neighbor = { { n2c->xy1.x - 1, y }, n->i + 1 };
            //ASNeighborListAdd(neighbors, &line_neighbor, static_cast<float>(xyi_distance_sq(*n, line_neighbor)));
        }
    } else {
        std::cerr << "Logic error for finding neighboring pixels..." << std::endl;
        abort();
    }
}

float RTreePixelPathNodeHeuristic(void *fromNode, void *toNode, void *context) {
    xyi* from = reinterpret_cast<xyi*>(fromNode);
    xyi* to = reinterpret_cast<xyi*>(toNode);
    return fabsf(static_cast<float>(from->p.x - to->p.x)) + fabsf(static_cast<float>(from->p.y - to->p.y));
}

int RTreePixelPathNodeComparator(void *node1, void *node2, void *context) {
    xyi* n1 = reinterpret_cast<xyi*>(node1);
    int n1v = n1->p.y << 16 | n1->p.x;
    xyi* n2 = reinterpret_cast<xyi*>(node2);
    int n2v = n2->p.y << 16 | n2->p.x;
    int d = n1v - n2v;
    if (d == 0) {
        return 0;
    } else if (d > 0) {
        return 1;
    } else {
        return -1;
    }
}

box_t box_t_from_xy(xy v) {
    return box_t(point_t(v.x, v.y), point_t(v.x + 1, v.y + 1));
}

void calculate_pixel_waypoints(xy from, xy to, ASPath cell_path) {
    ASPathNodeSource PathNodeSource =
    {
        sizeof(xyi),
        RTreePixelPathNodeNeighbors,
        RTreePixelPathNodeHeuristic,
        NULL,
        RTreePixelPathNodeComparator
    };
    size_t cell_path_count = ASPathGetCount(cell_path);
    if (cell_path_count == 0) {
        std::cerr << "calculate_pixel_waypoints: cell_path_count is 0." << std::endl;
        abort();
    }
    xyi from_rect = { from, 0 };
    xyi to_rect = { to, cell_path_count - 1 };
    pixel_waypoint_search pws = { from, to, cell_path };
    ASPath pixel_path = ASPathCreate(&PathNodeSource, &pws, &from_rect, &to_rect);
    size_t pixel_path_count = ASPathGetCount(pixel_path);
    if (pixel_path_count > 0) {
        printf("Path Count: %zu\n", pixel_path_count);
        float pixel_path_cost = ASPathGetCost(pixel_path);
        printf("Path Cost: %f\n", pixel_path_cost);
        /*if (pixel_path_cost < 6000)*/
        {
            for (size_t i = 0; i < pixel_path_count; i++) {
                xyi* pixel_node = reinterpret_cast<xyi*>(ASPathGetNode(pixel_path, i));
                printf("Pixel Path %zu: (%d, %d) [Cell index=%d]\n",
                       i,
                       pixel_node->p.x,
                       pixel_node->p.y,
                       pixel_node->i);
            }
        }
    } else {
        std::cerr << "No pixel waypoints found." << std::endl;
        abort();
    }
    ASPathDestroy(pixel_path);
}

void astar_rtree(const char* output, size_t output_max_size, xy from, xy to) {
    float distance = static_cast<float>(abs(from.x - to.x) + abs(from.y - to.y));
    std::cout << boost::format("%6%: Pathfinding from (%1%,%2%) -> (%3%,%4%) [distance = %5%]\n")
        % from.x % from.y % to.x % to.y % distance % output;
    bi::managed_mapped_file file(bi::open_or_create, output, output_max_size);
    allocator_t alloc(file.get_segment_manager());
    rtree_t* rtree_ptr = file.find_or_construct<rtree_t>("rtree")(params_t(), indexable_t(), equal_to_t(), alloc);
    printf("R Tree size: %zu\n", rtree_ptr->size());

    if (rtree_ptr->size() == 0) {
        abort();
    }

    auto from_box = box_t_from_xy(from);
    std::vector<value_t> from_result_s;
    rtree_ptr->query(bgi::contains(from_box), std::back_inserter(from_result_s));

    auto to_box = box_t_from_xy(to);
    std::vector<value_t> to_result_s;
    rtree_ptr->query(bgi::contains(to_box), std::back_inserter(to_result_s));

    auto s = rtree_ptr->qbegin(bgi::contains(to_box));

    if (from_result_s.size() == 1 && to_result_s.size() == 1) {
        // Phase 1 - R Tree rectangular node searching
        ASPathNodeSource PathNodeSource =
        {
            sizeof(xyxy),
            RTreePathNodeNeighbors,
            RTreePathNodeHeuristic,
            NULL,
            RTreePathNodeComparator
        };
        xyxy from_rect = xyxy_from_box_t(from_result_s[0].first);
        xyxy to_rect = xyxy_from_box_t(to_result_s[0].first);
        ASPath path = ASPathCreate(&PathNodeSource, rtree_ptr, &from_rect, &to_rect);
        size_t pathCount = ASPathGetCount(path);
        if (pathCount > 0) {
            printf("Cell Path Count: %zu\n", pathCount);
            float pathCost = ASPathGetCost(path);
            printf("Cell Path Cost: %f\n", pathCost);
            /*if (pathCost < 6000)*/
            {
                for (size_t i = 0; i < pathCount; i++) {
                    xyxy* node = reinterpret_cast<xyxy*>(ASPathGetNode(path, i));
                    printf("Cell Path %zu: (%d, %d)-(%d, %d) [%d x %d = %d]\n",
                           i,
                           node->xy0.x,
                           node->xy0.y,
                           node->xy1.x,
                           node->xy1.y,
                           node->xy1.x - node->xy0.x,
                           node->xy1.y - node->xy0.y,
                           (node->xy1.x - node->xy0.x) * (node->xy1.y - node->xy0.y));
                }
            }
            // Phase 2 - per-pixel node searching
            calculate_pixel_waypoints(from, to, path);
        } else {
            std::cerr << "No path found." << std::endl;
        }
        ASPathDestroy(path);
    } else {
        std::cerr << "From-node and/or to-node error." << std::endl;
    }
}
