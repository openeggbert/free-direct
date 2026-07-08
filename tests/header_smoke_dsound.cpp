/**
 * @file header_smoke_dsound.cpp
 * @brief Compile-only smoke test: include/dsound.h must compile standalone, as a real consumer
 * would include it, without pulling in any of free-direct's own .cpp implementation.
 * 24-Hour Stabilization Backlog TASK-24H-0017 (plan.md).
 */
#include <dsound.h>

int main() {
    return 0;
}
