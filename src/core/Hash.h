#pragma once
// Portable (no engine dependencies): this header is unit-tested on Linux.

#include <cstdint>
#include <string_view>

namespace sm
{
	/// 64-bit FNV-1a over the ASCII-lowercased id. Pack ids are compared case-insensitively
	/// through this hash so the OAR condition never has to allocate in the animation thread.
	/// 0 is reserved for "no pack active".
	constexpr std::uint64_t HashId(std::string_view a_id) noexcept
	{
		std::uint64_t h = 14695981039346656037ULL;
		for (const char ch : a_id) {
			auto c = static_cast<unsigned char>(ch);
			if (c >= 'A' && c <= 'Z') {
				c = static_cast<unsigned char>(c - 'A' + 'a');
			}
			h ^= c;
			h *= 1099511628211ULL;
		}
		return h == 0 ? 1 : h;
	}
}
