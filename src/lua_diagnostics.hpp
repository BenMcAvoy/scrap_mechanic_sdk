#pragma once

namespace scrap::sdk::lua::internal {

// Writes the existing debug-output and TEMP log records. It is intentionally
// independent of hook state so diagnostics remain usable during teardown.
void diagnostic_log(const char *format, ...) noexcept;

} // namespace scrap::sdk::lua::internal
