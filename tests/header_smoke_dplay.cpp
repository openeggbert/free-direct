/**
 * @file header_smoke_dplay.cpp
 * @brief Compile-only smoke test: include/dplay.h must compile standalone, as a real consumer
 * would include it, without pulling in any of free-direct's own .cpp implementation.
 * 24-Hour Stabilization Backlog TASK-24H-0018 (plan.md).
 *
 * A pre-existing -Wmissing-field-initializers warning on the two brace-initialized GUID
 * constants (IID_IDirectPlay / IID_IDirectPlay2A) is expected and not a new failure - see
 * docs/audit-24h-free-direct.md's build audit.
 */
#include <dplay.h>

int main() {
    return 0;
}
