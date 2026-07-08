/**
 * @file header_smoke_ddraw.cpp
 * @brief Compile-only smoke test: include/ddraw.h must compile standalone, as a real consumer
 * would include it, without pulling in any of free-direct's own .cpp implementation.
 * 24-Hour Stabilization Backlog TASK-24H-0016 (plan.md).
 */
#include <ddraw.h>

int main() {
    return 0;
}
