#pragma once

#include "gigchain/core/Error.h"

namespace gigchain::engine {

// Takes the current folder out of where Windows looks for DLLs a program
// asks for by name. Double-clicking a setlist starts the app in the
// setlist's folder (often Downloads): a DLL left there must never load.
// Plugins still load their own DLLs from next to them. Call first thing in
// main(); an error says why it could not (the app goes on).
core::Result<void> hardenDllSearch();

} // namespace gigchain::engine
