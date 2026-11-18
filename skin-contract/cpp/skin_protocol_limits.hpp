#pragma once
#include <cstddef>

// State snapshots may contain a complete graph and i18n catalog; requests stay
// independently bounded to avoid unbounded allocations.
inline constexpr std::size_t skin_max_event_bytes = 16u * 1024u * 1024u;
inline constexpr std::size_t skin_max_request_bytes = 1024u * 1024u;
