#pragma once
// Pulled in with -include by run_tests.sh / run_runtime_tests.sh. <windows.h> defines min / max as macros for
// every file compiled after it, which breaks a plain std::max(a, b) (error C2589 on MSVC). We include the
// headers the sources use first, then define the macros, so only our own code is affected - like on Windows.
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))
