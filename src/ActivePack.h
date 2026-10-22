#pragma once

#include <atomic>
#include <cstdint>

namespace sm
{
	/// HashId() of the pack that is currently active, or 0 when the mode is off.
	/// Written on the game's main thread, read lock-free from OAR's animation threads.
	inline std::atomic<std::uint64_t> g_activePackHash{ 0 };
}
