#include "Config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sm
{
	using nlohmann::json;

	namespace
	{
		constexpr std::array<Action, kActionCount> kAllActions{ Action::Enter, Action::Exit, Action::NextPack, Action::PrevPack };

		json BindingToJson(const Binding& a_binding)
		{
			json inputs = json::array();
			for (const auto& id : a_binding.inputs) {
				inputs.push_back(ToToken(id));
			}
			return json{
				{ "enabled", a_binding.enabled },
				{ "trigger", std::string(TriggerToken(a_binding.trigger)) },
				{ "timeMs", a_binding.timeMs },
				{ "inputs", std::move(inputs) }
			};
		}

		void ParseBinding(const json& a_node, std::string_view a_name, Binding& a_out, std::vector<std::string>& a_warnings)
		{
			if (!a_node.is_object()) {
				a_warnings.push_back(std::string(a_name) + ": not an object, using defaults");
				return;
			}
			if (const auto it = a_node.find("enabled"); it != a_node.end() && it->is_boolean()) {
				a_out.enabled = it->get<bool>();
			}
			if (const auto it = a_node.find("trigger"); it != a_node.end() && it->is_string()) {
				Trigger t;
				if (TriggerFromToken(it->get<std::string>(), t)) {
					a_out.trigger = t;
				} else {
					a_warnings.push_back(std::string(a_name) + ": unknown trigger '" + it->get<std::string>() + "'");
				}
			}
			if (const auto it = a_node.find("timeMs"); it != a_node.end() && it->is_number_integer()) {
				const auto v = it->get<std::int64_t>();
				a_out.timeMs = v <= 0 ? 0u : static_cast<std::uint32_t>((std::min<std::int64_t>)(v, kMaxTimeMs));
			}
			if (const auto it = a_node.find("inputs"); it != a_node.end() && it->is_array()) {
				a_out.inputs.clear();
				for (const auto& tok : *it) {
					if (!tok.is_string()) {
						a_warnings.push_back(std::string(a_name) + ": input is not a string");
						continue;
					}
					InputId id;
					if (!FromToken(tok.get<std::string>(), id)) {
						a_warnings.push_back(std::string(a_name) + ": unknown input '" + tok.get<std::string>() + "'");
						continue;
					}
					if (IsUnbindable(id)) {
						a_warnings.push_back(std::string(a_name) + ": input '" + tok.get<std::string>() + "' cannot be bound");
						continue;
					}
					if (std::find(a_out.inputs.begin(), a_out.inputs.end(), id) != a_out.inputs.end()) {
						continue;
					}
					if (a_out.inputs.size() >= kMaxChord) {
						a_warnings.push_back(std::string(a_name) + ": more than " + std::to_string(kMaxChord) + " inputs, extra ones ignored");
						break;
					}
					a_out.inputs.push_back(id);
				}
			}
		}

		void ReadBool(const json& a_obj, const char* a_key, bool& a_target)
		{
			if (const auto it = a_obj.find(a_key); it != a_obj.end() && it->is_boolean()) {
				a_target = it->get<bool>();
			}
		}
	}

	Config MakeDefaultConfig()
	{
		Config c;
		// Everything is unbound on purpose: nothing may fire until the user picks the inputs.
		c.bindings[static_cast<std::size_t>(Action::Enter)] = { true, Trigger::Hold, 0, {} };
		c.bindings[static_cast<std::size_t>(Action::Exit)] = { true, Trigger::Hold, 0, {} };
		c.bindings[static_cast<std::size_t>(Action::NextPack)] = { false, Trigger::Press, 0, {} };
		c.bindings[static_cast<std::size_t>(Action::PrevPack)] = { false, Trigger::Press, 0, {} };
		return c;
	}

	bool ParseConfig(std::string_view a_json, Config& a_out, std::vector<std::string>& a_warnings)
	{
		a_out = MakeDefaultConfig();

		const auto root = json::parse(a_json.begin(), a_json.end(), nullptr, false);
		if (root.is_discarded() || !root.is_object()) {
			return false;
		}

		if (const auto it = root.find("selectedPack"); it != root.end() && it->is_string()) {
			a_out.selectedPack = it->get<std::string>();
		}
		if (const auto it = root.find("language"); it != root.end() && it->is_string()) {
			const auto lang = it->get<std::string>();
			a_out.options.language = (lang == "en") ? "en" : "it";
		}
		if (const auto it = root.find("options"); it != root.end() && it->is_object()) {
			ReadBool(*it, "notify", a_out.options.notify);
			ReadBool(*it, "refreshOnSwap", a_out.options.refreshOnSwap);
			ReadBool(*it, "autoExitOnSheathe", a_out.options.autoExitOnSheathe);
			ReadBool(*it, "blockWhileAttacking", a_out.options.blockWhileAttacking);
			ReadBool(*it, "toggleWithEnter", a_out.options.toggleWithEnter);
		}
		if (const auto it = root.find("bindings"); it != root.end() && it->is_object()) {
			for (const auto action : kAllActions) {
				const auto name = ActionToken(action);
				if (const auto b = it->find(std::string(name)); b != it->end()) {
					ParseBinding(*b, name, a_out.bindings[static_cast<std::size_t>(action)], a_warnings);
				}
			}
		}
		return true;
	}

	std::string SerializeConfig(const Config& a_config)
	{
		json bindings = json::object();
		for (const auto action : kAllActions) {
			bindings[std::string(ActionToken(action))] = BindingToJson(a_config.bindings[static_cast<std::size_t>(action)]);
		}
		const json root{
			{ "version", 1 },
			{ "language", a_config.options.language },
			{ "selectedPack", a_config.selectedPack },
			{ "options",
				{ { "notify", a_config.options.notify },
					{ "refreshOnSwap", a_config.options.refreshOnSwap },
					{ "autoExitOnSheathe", a_config.options.autoExitOnSheathe },
					{ "blockWhileAttacking", a_config.options.blockWhileAttacking },
					{ "toggleWithEnter", a_config.options.toggleWithEnter } } },
			{ "bindings", std::move(bindings) }
		};
		return root.dump(2) + "\n";
	}

	bool LoadConfigFile(const std::filesystem::path& a_path, Config& a_out, std::vector<std::string>& a_warnings)
	{
		a_out = MakeDefaultConfig();
		std::ifstream in(a_path, std::ios::binary);
		if (!in) {
			return false;
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		return ParseConfig(ss.str(), a_out, a_warnings);
	}

	bool SaveConfigFile(const std::filesystem::path& a_path, const Config& a_config, std::string& a_error)
	{
		const auto text = SerializeConfig(a_config);

		std::error_code ec;
		std::filesystem::create_directories(a_path.parent_path(), ec);

		const auto tmp = std::filesystem::path(a_path).concat(".tmp");
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (out) {
				out << text;
				out.close();
				if (out) {
					std::filesystem::rename(tmp, a_path, ec);
					if (!ec) {
						return true;
					}
				}
			}
		}
		std::filesystem::remove(tmp, ec);

		// Fallback (some virtual file systems dislike rename): write in place.
		std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
		if (!out) {
			a_error = "cannot open '" + a_path.string() + "' for writing";
			return false;
		}
		out << text;
		out.close();
		if (!out) {
			a_error = "write to '" + a_path.string() + "' failed";
			return false;
		}
		return true;
	}
}
