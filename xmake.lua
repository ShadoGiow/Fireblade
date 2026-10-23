-- Swap Moveset - SKSE plugin (CommonLibSSE-NG, Skyrim SE / AE via Address Library)
--
-- Build (Windows, MSVC):  xmake f -m releasedbg -y   then   xmake -y
-- CommonLibSSE-NG is expected in lib/commonlibsse-ng (the GitHub workflow checks out the pinned commit).
set_xmakever("3.0.0")

includes("lib/commonlibsse-ng")

set_project("SwapMoveset")
set_version("1.0.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.releasedbg")

add_requires("nlohmann_json")

target("SwapMoveset")
    add_rules("commonlibsse-ng.plugin", {
        name = "SwapMoveset",
        author = "SwapMoveset",
        description = "Switch to a different animation moveset with a key / controller combination",
        options = {
            address_library = true
        }
    })

    add_packages("nlohmann_json")

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
