#include "PackManifest.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sm
{
	using nlohmann::json;

	bool IsValidPackId(std::string_view a_id) noexcept
	{
		if (a_id.empty() || a_id.size() > 48) {
			return false;
		}
		return std::ranges::all_of(a_id, [](char c) {
			return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
		});
	}

	bool ParsePackManifest(std::string_view a_json, PackInfo& a_out, std::string& a_error)
	{
		const auto root = json::parse(a_json.begin(), a_json.end(), nullptr, false);
		if (root.is_discarded() || !root.is_object()) {
			a_error = "not a valid JSON object";
			return false;
		}

		const auto str = [&](const char* key) -> std::string {
			if (const auto it = root.find(key); it != root.end() && it->is_string()) {
				return it->get<std::string>();
			}
			return {};
		};

		PackInfo p;
		p.id = str("id");
		if (!IsValidPackId(p.id)) {
			a_error = "missing or invalid 'id' (allowed: a-z 0-9 _ -, max 48 chars)";
			return false;
		}
		p.name = str("name");
		if (p.name.empty()) {
			p.name = p.id;
		}
		p.author = str("author");
		p.description = str("description");
		p.notes = str("notes");
		if (const auto it = root.find("order"); it != root.end() && it->is_number_integer()) {
			p.order = static_cast<int>(std::clamp<std::int64_t>(it->get<std::int64_t>(), -100000, 100000));
		}
		a_out = std::move(p);
		return true;
	}

	std::vector<PackInfo> ScanPacks(const std::filesystem::path& a_dir, std::vector<std::string>& a_warnings)
	{
		std::vector<PackInfo> packs;

		std::error_code ec;
		if (!std::filesystem::is_directory(a_dir, ec)) {
			return packs;
		}

		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(a_dir, ec)) {
			if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
				files.push_back(entry.path());
			}
		}
		std::sort(files.begin(), files.end());

		for (const auto& file : files) {
			std::ifstream in(file, std::ios::binary);
			if (!in) {
				a_warnings.push_back(file.filename().string() + ": cannot open");
				continue;
			}
			std::ostringstream ss;
			ss << in.rdbuf();

			PackInfo    info;
			std::string err;
			if (!ParsePackManifest(ss.str(), info, err)) {
				a_warnings.push_back(file.filename().string() + ": " + err);
				continue;
			}
			info.file = file;
			if (std::ranges::any_of(packs, [&](const PackInfo& p) { return p.id == info.id; })) {
				a_warnings.push_back(file.filename().string() + ": duplicate pack id '" + info.id + "', ignored");
				continue;
			}
			packs.push_back(std::move(info));
		}

		std::stable_sort(packs.begin(), packs.end(), [](const PackInfo& a, const PackInfo& b) {
			return a.order != b.order ? a.order < b.order : a.name < b.name;
		});
		return packs;
	}
}
