#pragma once

struct xy {
    int x : 16;
    int y : 16;
};

struct xy32 {
    int x;
    int y;
};

inline bool operator < (const xy& lhs, const xy& rhs) {
    return (lhs.y << 16 | lhs.x) < (rhs.y << 16 | rhs.x);
}

struct xyxy {
    xy xy0;
    xy xy1;
};

struct xy32xy32 {
    xy32 xy0;
    xy32 xy1;
};

struct xyi {
    xy p;
    size_t i;
};

enum XYIB_ENTER_EXIT {
    XEE_ENTER,
    XEE_EXIT,
};

struct xyib {
    xy p;
    size_t i;
    XYIB_ENTER_EXIT ee;
};
