#pragma once

struct xy {
    int x : 16;
    int y : 16;
};

inline bool operator < (const xy& lhs, const xy& rhs) {
    return (lhs.y << 16 | lhs.x) < (rhs.y << 16 | rhs.x);
}

struct xyxy {
    xy xy0;
    xy xy1;
};

struct xyi {
    xy p;
    size_t i;
};
