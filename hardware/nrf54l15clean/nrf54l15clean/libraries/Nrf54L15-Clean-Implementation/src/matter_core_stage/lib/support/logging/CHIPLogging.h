#pragma once

// The staged Arduino target has no CHIP logging backend yet. Keep logging
// calls compile-time silent while preserving all checks and return paths.
#define ChipLogError(module, format, ...) ((void)0)
#define ChipLogProgress(module, format, ...) ((void)0)
#define ChipLogDetail(module, format, ...) ((void)0)
#define ChipLogAutomation(module, format, ...) ((void)0)
