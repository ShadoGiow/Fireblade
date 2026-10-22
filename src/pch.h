#pragma once

// CommonLib first, Windows SDK never (we use REX::W32 + dynamic loading instead).
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace logs = SKSE::log;
using namespace std::literals;
