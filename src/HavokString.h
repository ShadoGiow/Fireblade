#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>
#include "RE/H/hkStringPtr.h"

namespace bf::havokstring {
// Havok tags strings in bit 0. The complement MUST be pointer-width:
// ~uint32_t{1} would zero the high half of a 64-bit address after promotion.
constexpr std::uintptr_t Untag(std::uintptr_t tagged) noexcept {
    return tagged & ~std::uintptr_t{1};
}
static_assert(sizeof(std::uintptr_t)==8);
static_assert(sizeof(RE::hkStringPtr)==sizeof(std::uintptr_t));
static_assert(Untag(0x000001ECD0989011ULL)==0x000001ECD0989010ULL);
static_assert(Untag(0x000001ECD0989010ULL)==0x000001ECD0989010ULL);
static_assert(Untag(0x00007FFA00568089ULL)==0x00007FFA00568088ULL);
static_assert(Untag(0)==0 && Untag(1)==0);
// Reproduce the failure mode seen in the crash: high address bits are lost.
static_assert((0x000001ECD0989011ULL & ~std::uint32_t{1})==0xD0989010ULL);

inline const char* View(const RE::hkStringPtr& value) noexcept {
    std::uintptr_t tagged{};
    std::memcpy(&tagged,&value,sizeof(tagged));
    return reinterpret_cast<const char*>(Untag(tagged));
}
}
