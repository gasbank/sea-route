#pragma once

struct xy32 {
    int x;
    int y;
};

inline bool operator < (const xy32& lhs, const xy32& rhs) {
    return (lhs.y << 16 | lhs.x) < (rhs.y << 16 | rhs.x);
}

struct xy32xy32 {
    xy32 xy0;
    xy32 xy1;
};

struct xy32i {
    xy32 p;
    size_t i;
};

enum XYIB_ENTER_EXIT {
    XEE_ENTER,
    XEE_EXIT,
};

struct xy32ib {
    xy32 p;
    size_t i;
    XYIB_ENTER_EXIT ee;
};
