#pragma once
// Portable (no engine dependencies except nlohmann/json): unit-tested on Linux.

#include "Binding.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sm
{
	struct Options
	{
		bool        notify{ true };               // on-screen message when the mode changes
		bool        refreshOnSwap{ true };        // kick the animation graph so looping animations switch at once
		bool        autoExitOnSheathe{ false };   // leave the mode when the weapon is sheathed (after it was drawn)
		bool        blockWhileAttacking{ true };  // ignore enter/exit/cycle while an attack is in progress
		bool        toggleWithEnter{ true };      // the Enter binding also exits (Exit binding unused)
		std::string language{ "it" };             // "it" | "en"
	};

	struct Config
	{
		std::string selectedPack;  // pack id used by "Enter"
		Options     options;
		BindingSet  bindings;
	};

	[[nodiscard]] Config MakeDefaultConfig();

	/// Lenient parser: bad fields produce a warning and keep their default. Returns false only when the
	/// text is not a JSON object (a_out is then the default config).
	bool ParseConfig(std::string_view a_json, Config& a_out, std::vector<std::string>& a_warnings);
	[[nodiscard]] std::string SerializeConfig(const Config& a_config);

	/// false = file missing or unreadable/invalid (a_out is the default config in that case).
	bool LoadConfigFile(const std::filesystem::path& a_path, Config& a_out, std::vector<std::string>& a_warnings);
	bool SaveConfigFile(const std::filesystem::path& a_path, const Config& a_config, std::string& a_error);
}
