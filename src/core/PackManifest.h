#pragma once
// Portable (no engine dependencies except nlohmann/json): unit-tested on Linux.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sm
{
	/// One moveset pack = an Open Animation Replacer mod whose conditions use the SwapMoveset_PackActive
	/// condition, plus this small manifest (Data/SKSE/Plugins/SwapMoveset/Packs/<anything>.json).
	struct PackInfo
	{
		std::string           id;           // [a-z0-9_-], matches the "Pack Id" in the OAR config.json files
		std::string           name;         // shown in the menu
		std::string           author;
		std::string           description;
		std::string           notes;        // free text: requirements, conversion notes
		int                   order{ 100 }; // list order, lower first
		std::filesystem::path file;         // manifest path (diagnostics)
	};

	[[nodiscard]] bool IsValidPackId(std::string_view a_id) noexcept;

	/// false + a_error when the text is not a valid manifest.
	bool ParsePackManifest(std::string_view a_json, PackInfo& a_out, std::string& a_error);

	/// Reads every *.json in a_dir. Invalid manifests and duplicate ids are skipped with a warning.
	/// Sorted by (order, name).
	std::vector<PackInfo> ScanPacks(const std::filesystem::path& a_dir, std::vector<std::string>& a_warnings);
}
