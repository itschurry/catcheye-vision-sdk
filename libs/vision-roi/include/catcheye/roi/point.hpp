#pragma once

namespace catcheye::roi {

struct Point {
    double x {0.0};
    double y {0.0};
};

inline bool operator==(const Point& lhs, const Point& rhs)
{
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

inline bool operator!=(const Point& lhs, const Point& rhs)
{
    return !(lhs == rhs);
}

} // namespace catcheye::roi
