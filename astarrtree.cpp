#include "precompiled.hpp"
#include "astarrtree.hpp"
#include "AStar.h"

using namespace astarrtree;

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


float xyib_distance(const xyib& a, const xyib& b) {
    int dx = a.p.x - b.p.x;
    int dy = a.p.y - b.p.y;
    return static_cast<float>(abs(dx) + abs(dy)); // sqrtf(static_cast<float>(dx * dx + dy * dy));
}

struct pixel_waypoint_search {
    xy from;
    xy to;
    ASPath cell_path;
};

void RTreePixelPathNodeNeighbors(ASNeighborList neighbors, void *node, void *context);

void AddNeighborWithLog(ASNeighborList neighbors, xyib* n, pixel_waypoint_search* pws, int x, int y, size_t i, XYIB_ENTER_EXIT ee) {
    xyib neighbor = { { x, y }, i, ee };
    if (n->p.x == x && n->p.y == y && n->ee == XEE_ENTER && ee == XEE_EXIT && n->i == i) {
        // if 'n' and 'neighbor' have the same coordinates...
        // make 'n' exit node and rerun RTreePixelPathNodeNeighbors()
        n->ee = XEE_EXIT;
        RTreePixelPathNodeNeighbors(neighbors, n, pws);
    } else {
        ASNeighborListAdd(neighbors, &neighbor, xyib_distance(*n, neighbor));
        /*printf("Neighbor of (%5d,%5d)[%5d] : (%5d,%5d)[%5d]\n",
        n->p.x,
        n->p.y,
        n->i,
        x,
        y,
        i);*/
    }
}

enum RECT_RELATION {
    RR_DOWN_RIGHT,
    RR_UP_RIGHT,
    RR_UP_LEFT,
    RR_DOWN_LEFT,
    RR_DOWN,
    RR_UP,
    RR_RIGHT,
    RR_LEFT,
    RR_UNKNOWN,
};


RECT_RELATION rect_relation(const xyxy* n1c, const xyxy* n2c) {
    bool d = false, u = false, r = false, l = false;
    if (n1c->xy1.y <= n2c->xy0.y) {
        // [D]OWN
        //  n1c
        // -----
        //  n2c
        d = true;
    }
    if (n2c->xy1.y <= n1c->xy0.y) {
        // [U]P
        //  n2c
        // -----
        //  n1c
        u = true;
    }
    if (n1c->xy1.x <= n2c->xy0.x) {
        // [R]IGHT
        //      |
        //  n1c | n2c
        //      |
        r = true;
    }
    if (n2c->xy1.x <= n1c->xy0.x) {
        // [L]EFT
        //      |
        //  n2c | n1c
        //      |
        l = true;
    }
    if (d && !u && r && !l) {
        return RR_DOWN_RIGHT;
    } else if (!d && u && r && !l) {
        return RR_UP_RIGHT;
    } else if (!d && u && !r && l) {
        return RR_UP_LEFT;
    } else if (d && !u && !r && l) {
        return RR_DOWN_LEFT;
    } else if (d && !u && !r && !l) {
        return RR_DOWN;
    } else if (!d && u && !r && !l) {
        return RR_UP;
    } else if (!d && !u && r && !l) {
        return RR_RIGHT;
    } else if (!d && !u && !r && l) {
        return RR_LEFT;
    } else {
        std::cerr << "Unknown rect relation..." << std::endl;
        return RR_UNKNOWN;
    }
}

RECT_RELATION rect_neighbor_relation(const xyxy* n1c, const xyxy* n2c) {
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
        return RR_DOWN_RIGHT;
    } else if (!d && u && r && !l) {
        return RR_UP_RIGHT;
    } else if (!d && u && !r && l) {
        return RR_UP_LEFT;
    } else if (d && !u && !r && l) {
        return RR_DOWN_LEFT;
    } else if (d && !u && !r && !l) {
        return RR_DOWN;
    } else if (!d && u && !r && !l) {
        return RR_UP;
    } else if (!d && !u && r && !l) {
        return RR_RIGHT;
    } else if (!d && !u && !r && l) {
        return RR_LEFT;
    } else {
        std::cerr << "Unknown rect relation..." << std::endl;
        return RR_UNKNOWN;
    }
}

void RTreePixelPathNodeNeighbors(ASNeighborList neighbors, void *node, void *context) {
    xyib* n = reinterpret_cast<xyib*>(node);
    pixel_waypoint_search* pws = reinterpret_cast<pixel_waypoint_search*>(context);
    ASPath cell_path = pws->cell_path;
    size_t cell_path_count = ASPathGetCount(cell_path);
    if (n->p.x == pws->to.x && n->p.y == pws->to.y) {
        // 'n' equals to 'to': reached endpoint (to-pixel)
        // no neighbors on endpoint
        return;
    } else if (n->i == cell_path_count - 1 && n->ee == XEE_ENTER) {
        // all last cells' pixels (except endpoint pixel) always have the only neighbor: endpoint pixel
        AddNeighborWithLog(neighbors, n, pws, pws->to.x, pws->to.y, cell_path_count - 1, XEE_EXIT);
        return;
    }
    xyxy* n1c;
    xyxy* n2c;
    XYIB_ENTER_EXIT next_ee = XEE_ENTER;
    int next_i = 0;
    if (n->ee == XEE_ENTER) {
        // Get exit nodes at the same cell node
        n1c = reinterpret_cast<xyxy*>(ASPathGetNode(cell_path, n->i + 1)); // Next Cell node
        n2c = reinterpret_cast<xyxy*>(ASPathGetNode(cell_path, n->i + 0)); // Cell node containing 'n'
        next_ee = XEE_EXIT;
        next_i = n->i; // the same cell node
    } else if (n->ee == XEE_EXIT) {
        // Get enter nodes at the next cell node
        n1c = reinterpret_cast<xyxy*>(ASPathGetNode(cell_path, n->i + 0)); // Cell node containing 'n'
        n2c = reinterpret_cast<xyxy*>(ASPathGetNode(cell_path, n->i + 1)); // Next Cell node
        next_ee = XEE_ENTER;
        next_i = n->i + 1; // the next cell node
    } else {
        std::cerr << "Corrupted ee" << std::endl;
        abort();
    }

    switch (rect_neighbor_relation(n1c, n2c)) {
    case RR_DOWN_RIGHT:
        AddNeighborWithLog(neighbors, n, pws, n2c->xy0.x, n2c->xy0.y, next_i, next_ee);
        break;
    case RR_UP_RIGHT:
        AddNeighborWithLog(neighbors, n, pws, n2c->xy0.x, n2c->xy1.y - 1, next_i, next_ee);
        break;
    case RR_UP_LEFT:
        AddNeighborWithLog(neighbors, n, pws, n2c->xy1.x - 1, n2c->xy1.y - 1, next_i, next_ee);
        break;
    case RR_DOWN_LEFT:
        AddNeighborWithLog(neighbors, n, pws, n2c->xy1.x - 1, n2c->xy0.y, next_i, next_ee);
        break;
    case RR_DOWN:
        for (int x = std::max(n1c->xy0.x, n2c->xy0.x); x < std::min(n1c->xy1.x, n2c->xy1.x); x++) {
            AddNeighborWithLog(neighbors, n, pws, x, n2c->xy0.y, next_i, next_ee);
        }
        break;
    case RR_UP:
        for (int x = std::max(n1c->xy0.x, n2c->xy0.x); x < std::min(n1c->xy1.x, n2c->xy1.x); x++) {
            AddNeighborWithLog(neighbors, n, pws, x, n2c->xy1.y - 1, next_i, next_ee);
        }
        break;
    case RR_RIGHT:
        for (int y = std::max(n1c->xy0.y, n2c->xy0.y); y < std::min(n1c->xy1.y, n2c->xy1.y); y++) {
            AddNeighborWithLog(neighbors, n, pws, n2c->xy0.x, y, next_i, next_ee);
        }
        break;
    case RR_LEFT:
        for (int y = std::max(n1c->xy0.y, n2c->xy0.y); y < std::min(n1c->xy1.y, n2c->xy1.y); y++) {
            AddNeighborWithLog(neighbors, n, pws, n2c->xy1.x - 1, y, next_i, next_ee);
        }
        break;
    case RR_UNKNOWN:
    default:
        std::cerr << "Logic error for finding neighboring pixels..." << std::endl;
        abort();
        break;
    }
}

float RTreePixelPathNodeHeuristic(void *fromNode, void *toNode, void *context) {
    xyib* from = reinterpret_cast<xyib*>(fromNode);
    xyib* to = reinterpret_cast<xyib*>(toNode);
    return fabsf(static_cast<float>(from->p.x - to->p.x)) + fabsf(static_cast<float>(from->p.y - to->p.y));
}

int RTreePixelPathNodeComparator(void *node1, void *node2, void *context) {
    xyib* n1 = reinterpret_cast<xyib*>(node1);
    int n1v = n1->p.y << 16 | n1->p.x;
    xyib* n2 = reinterpret_cast<xyib*>(node2);
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

xyxy xyxy_from_xy(xy v) {
    return xyxy{ { v.x, v.y },{ v.x + 1, v.y + 1 } };
}

std::vector<xy> calculate_pixel_waypoints(xy from, xy to, ASPath cell_path) {
    std::vector<xy> waypoints;
    ASPathNodeSource PathNodeSource =
    {
        sizeof(xyib),
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
    xyib from_rect = { from, 0, XEE_ENTER };
    xyib to_rect = { to, cell_path_count - 1, XEE_EXIT };
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
                xyib* pixel_node = reinterpret_cast<xyib*>(ASPathGetNode(pixel_path, i));
                printf("Pixel Path %zu: (%d, %d) [Cell index=%d]\n",
                       i,
                       pixel_node->p.x,
                       pixel_node->p.y,
                       pixel_node->i);
                waypoints.push_back(pixel_node->p);
            }
        }
    } else {
        std::cerr << "No pixel waypoints found." << std::endl;
    }
    ASPathDestroy(pixel_path);
    return waypoints;
}

void astarrtree::astar_rtree(const char* rtree_filename, size_t output_max_size, xy from, xy to) {
    bi::managed_mapped_file file(bi::open_or_create, rtree_filename, output_max_size);
    allocator_t alloc(file.get_segment_manager());
    rtree_t* rtree_ptr = file.find_or_construct<rtree_t>("rtree")(params_t(), indexable_t(), equal_to_t(), alloc);
    astar_rtree_memory(rtree_ptr, from, to);
}

bool find_nearest_point_if_empty(rtree_t* rtree_ptr, xy& from, box_t& from_box, std::vector<value_t>& from_result_s) {
    if (from_result_s.size() == 0) {
        auto nearest_it = rtree_ptr->qbegin(bgi::nearest(from_box, 1));
        if (nearest_it == rtree_ptr->qend()) {
            std::cerr << "Empty result from nearest query..." << std::endl;
            abort();
        }
        auto nearest_xyxy = xyxy_from_box_t(nearest_it->first);
        auto from_xyxy = xyxy_from_xy(from);
        switch (rect_relation(&nearest_xyxy, &from_xyxy)) {
        case RR_DOWN_RIGHT:
            from = { nearest_xyxy.xy1.x - 1, nearest_xyxy.xy1.y - 1 };
            from_box = box_t_from_xy(from);
            from_result_s.push_back(*nearest_it);
            break;
        case RR_UP_RIGHT:
            from = { nearest_xyxy.xy1.x - 1, nearest_xyxy.xy0.y };
            from_box = box_t_from_xy(from);
            from_result_s.push_back(*nearest_it);
            break;
        case RR_UP_LEFT:
            from = { nearest_xyxy.xy0.x, nearest_xyxy.xy0.y };
            from_box = box_t_from_xy(from);
            from_result_s.push_back(*nearest_it);
            break;
        case RR_DOWN_LEFT:
            from = { nearest_xyxy.xy0.x, nearest_xyxy.xy1.y - 1 };
            from_box = box_t_from_xy(from);
            from_result_s.push_back(*nearest_it);
            break;
        case RR_DOWN:
            from = { from.x, nearest_xyxy.xy1.y - 1 };
            from_box = box_t_from_xy(from);
            from_result_s.push_back(*nearest_it);
            break;
        case RR_UP:
            from = { from.x, nearest_xyxy.xy0.y };
            from_box = box_t_from_xy(from);
            from_result_s.push_back(*nearest_it);
            break;
        case RR_RIGHT:
            from = { nearest_xyxy.xy1.x - 1, from.y };
            from_box = box_t_from_xy(from);
            from_result_s.push_back(*nearest_it);
            break;
        case RR_LEFT:
            from = { nearest_xyxy.xy0.x, from.y };
            from_box = box_t_from_xy(from);
            from_result_s.push_back(*nearest_it);
            break;
        case RR_UNKNOWN:
        default:
            std::cerr << "rect relation unknown..." << std::endl;
            abort();
            break;
        }
        return true;
    } else {
        return false;
    }
}

std::vector<xy> astarrtree::astar_rtree_memory(rtree_t* rtree_ptr, xy from, xy to) {
    float distance = static_cast<float>(abs(from.x - to.x) + abs(from.y - to.y));
    std::cout << boost::format("Pathfinding from (%1%,%2%) -> (%3%,%4%) [distance = %5%]\n")
        % from.x % from.y % to.x % to.y % distance;

    std::vector<xy> waypoints;
    printf("R Tree size: %zu\n", rtree_ptr->size());
    if (rtree_ptr->size() == 0) {
        return waypoints;
    }

    auto from_box = box_t_from_xy(from);
    std::vector<value_t> from_result_s;
    rtree_ptr->query(bgi::contains(from_box), std::back_inserter(from_result_s));
    if (find_nearest_point_if_empty(rtree_ptr, from, from_box, from_result_s)) {
        std::cout << boost::format("  'From' point changed to (%1%,%2%)\n") % from.x % from.y;
    }

    auto to_box = box_t_from_xy(to);
    std::vector<value_t> to_result_s;
    rtree_ptr->query(bgi::contains(to_box), std::back_inserter(to_result_s));
    if (find_nearest_point_if_empty(rtree_ptr, to, to_box, to_result_s)) {
        std::cout << boost::format("  'To' point changed to (%1%,%2%)\n") % to.x % to.y;
    }

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
            waypoints = calculate_pixel_waypoints(from, to, path);
        } else {
            std::cerr << "No path found." << std::endl;
        }
        ASPathDestroy(path);
    } else {
        std::cerr << "From-node and/or to-node error." << std::endl;
    }
    return waypoints;
}
