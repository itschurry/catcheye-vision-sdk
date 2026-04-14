#include "test_support.hpp"

#include <cmath>

#include "catcheye/roi/roi_geometry.hpp"

using namespace catcheye::roi;

TEST_CASE(geometry_point_in_concave_polygon)
{
    const std::vector<Point> polygon {{0.0, 0.0}, {6.0, 0.0}, {6.0, 2.0}, {2.0, 2.0}, {2.0, 6.0}, {0.0, 6.0}};

    test_support::assert_true(point_in_polygon({1.0, 1.0}, polygon), "point should be inside");
    test_support::assert_true(!point_in_polygon({4.0, 4.0}, polygon), "point should be outside concave region");
}

TEST_CASE(geometry_area_and_bounds)
{
    const std::vector<Point> polygon {{1.0, 1.0}, {5.0, 1.0}, {5.0, 4.0}, {1.0, 4.0}};

    const double area = polygon_area(polygon);
    const auto bounds = polygon_bounds(polygon);

    test_support::assert_true(std::fabs(area - 12.0) < 1e-9, "area mismatch");
    test_support::assert_true(bounds.valid, "bounds should be valid");
    test_support::assert_true(bounds.min_x == 1.0 && bounds.min_y == 1.0, "bounds min mismatch");
    test_support::assert_true(bounds.max_x == 5.0 && bounds.max_y == 4.0, "bounds max mismatch");
}

TEST_CASE(geometry_detects_self_intersection)
{
    const std::vector<Point> polygon {{0.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}, {4.0, 0.0}};
    test_support::assert_true(has_self_intersections(polygon), "expected self-intersection");
}
