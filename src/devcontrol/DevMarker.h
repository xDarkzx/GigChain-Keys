#pragma once

namespace gigchain::devcontrol {

// Found in every program built with the live test control, and in no other:
// packaging fails when a program it would ship contains it (tools/package.ps1).
[[nodiscard]] const char* marker() noexcept;

} // namespace gigchain::devcontrol
