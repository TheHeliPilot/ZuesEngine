#pragma once
#include <cstdint>
#include <bitset>

// Define the maximum number of unique component types your engine will support.
// This is the size of the std::bitset used for signatures.
constexpr uint32_t MAX_COMPONENTS = 64;

// Type alias for component set tracking
using ComponentSignature = std::bitset<MAX_COMPONENTS>;