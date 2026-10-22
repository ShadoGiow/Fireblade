#include "Common.h"

namespace ESA
{
	Settings S;

	namespace
	{
		constexpr auto kIniPath = "Data/SKSE/Plugins/FireBladeStandalone.ini";

		std::string Trim(std::string a_s)
		{
			const auto b = a_s.find_first_not_of(" \t\r\n");
			if (b == std::string::npos) {
				return {};
			}
			const auto e = a_s.find_last_not_of(" \t\r\n");
			return a_s.substr(b, e - b + 1);
		}

		using Map = std::map<std::string, std::string>;

		Map ReadFile()
		{
			Map          out;
			std::ifstream in(kIniPath);
			if (!in) {
				return out;
			}
			std::string line, section;
			while (std::getline(in, line)) {
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#') {
					continue;
				}
				if (line.front() == '[' && line.back() == ']') {
					section = line.substr(1, line.size() - 2);
					continue;
				}
				const auto eq = line.find('=');
				if (eq == std::string::npos) {
					continue;
				}
				out[section + "." + Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
			}
			return out;
		}

		struct Reader
		{
			Map& m;

			void Get(const char* a_key, bool& a_v) const
			{
				if (auto it = m.find(a_key); it != m.end()) {
					a_v = !(it->second == "0" || it->second == "false" || it->second == "False");
				}
			}
			void Get(const char* a_key, int& a_v) const
			{
				if (auto it = m.find(a_key); it != m.end()) {
					try { a_v = std::stoi(it->second); } catch (...) {}
				}
			}
			void Get(const char* a_key, float& a_v) const
			{
				if (auto it = m.find(a_key); it != m.end()) {
					try { a_v = std::stof(it->second); } catch (...) {}
				}
			}
			void Get(const char* a_key, std::string& a_v) const
			{
				if (auto it = m.find(a_key); it != m.end()) {
					a_v = it->second;
				}
			}
			void Get(const char* a_key, std::uint32_t& a_v) const
			{
				if (auto it = m.find(a_key); it != m.end()) {
					try { a_v = static_cast<std::uint32_t>(std::stoul(it->second, nullptr, 0)); } catch (...) {}
				}
			}
		};
	}

	void Ini::Load()
	{
		Map    m = ReadFile();
		Reader r{ m };

		r.Get("General.TargetPlugin", S.pluginFile);
		r.Get("General.Enabled", S.enabled);
		r.Get("General.Verbose", S.verbose);

		r.Get("Stance.Mode", S.stanceMode);

		r.Get("Input.PadEnabled", S.padEnabled);
		r.Get("Input.UltChordMask", S.ultChord);
		r.Get("Input.UltNeedsNeutralStick", S.ultNeutralStick);
		r.Get("Input.StickDeadzone", S.deadzone);
		r.Get("Input.PadForwardIsComb", S.padForwardComb);
		r.Get("Input.PadBackIsExecute", S.padBackExec);
		r.Get("Input.KeyboardFallback", S.kbFallback);

		r.Get("Resources.MinMagickaSkill", S.magSkill);
		r.Get("Resources.MinMagickaUltSkill", S.magUltSkill);
		r.Get("Resources.MinMagickaUlt", S.magUlt);

		r.Get("Chase.AutoFollowUp", S.chaseAuto);
		r.Get("Chase.Delay", S.chaseDelay);

		r.Get("Incineration.Enabled", S.incEnabled);
		r.Get("Incineration.ExtraCasts", S.incExtra);
		r.Get("Incineration.Distance", S.incDist);
		r.Get("Incineration.Step", S.incStep);
		r.Get("Incineration.Delay", S.incDelay);

		r.Get("Burst.Enabled", S.burstEnabled);

		r.Get("ReIgnition.Enabled", S.reignEnabled);
		r.Get("ReIgnition.HealthPercent", S.reignPercent);
		r.Get("ReIgnition.CombatOnly", S.reignNeedCombat);

		r.Get("Visual.GhostBlade", S.ghostBlade);

		S.stanceMode = std::clamp(S.stanceMode, 0, 3);
		S.deadzone = std::clamp(S.deadzone, 0.05f, 0.95f);
		S.incExtra = std::clamp(S.incExtra, 0, 6);
		S.reignPercent = std::clamp(S.reignPercent, 1, 99);
		S.chaseDelay = std::clamp(S.chaseDelay, 0.0f, 3.0f);
	}

	void Ini::Save()
	{
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(kIniPath).parent_path(), ec);

		std::ofstream o(kIniPath, std::ios::trunc);
		if (!o) {
			Logf("impossibile scrivere {}", kIniPath);
			return;
		}
		auto b = [](bool v) { return v ? 1 : 0; };

		o << "; FireBladeStandalone - settings (also editable from the F1 menu)\n";
		o << "[General]\n; name of the data plugin that holds the forms (required)\nTargetPlugin=" << S.pluginFile << "\nEnabled=" << b(S.enabled) << "\nVerbose=" << b(S.verbose) << "\n\n";
		o << "[Stance]\n; 0 = fire spell on the left, 1 = any destruction spell, 2 = any spell, 3 = blade alone is enough\n";
		o << "Mode=" << S.stanceMode << "\n\n";
		o << "[Input]\n; chord mask for the ultimate (XInput bits: L3=0x40 R3=0x80 LB=0x100 RB=0x200 Back=0x20 Start=0x10\n";
		o << "; A=0x1000 B=0x2000 X=0x4000 Y=0x8000 DUp=1 DDown=2 DLeft=4 DRight=8 LT=0x10000 RT=0x20000)\n";
		o << "PadEnabled=" << b(S.padEnabled) << "\nUltChordMask=0x" << std::hex << S.ultChord << std::dec << "\n";
		o << "UltNeedsNeutralStick=" << b(S.ultNeutralStick) << "\nStickDeadzone=" << S.deadzone << "\n";
		o << "PadForwardIsComb=" << b(S.padForwardComb) << "\nPadBackIsExecute=" << b(S.padBackExec) << "\n";
		o << "KeyboardFallback=" << b(S.kbFallback) << "\n\n";
		o << "[Resources]\n; minimum current magicka for the skill variants (0 = no requirement)\n";
		o << "MinMagickaSkill=" << S.magSkill << "\nMinMagickaUltSkill=" << S.magUltSkill << "\nMinMagickaUlt=" << S.magUlt << "\n\n";
		o << "[Chase]\nAutoFollowUp=" << b(S.chaseAuto) << "\nDelay=" << S.chaseDelay << "\n\n";
		o << "[Incineration]\nEnabled=" << b(S.incEnabled) << "\nExtraCasts=" << S.incExtra << "\nDistance=" << S.incDist
		  << "\nStep=" << S.incStep << "\nDelay=" << S.incDelay << "\n\n";
		o << "[Burst]\nEnabled=" << b(S.burstEnabled) << "\n\n";
		o << "[ReIgnition]\nEnabled=" << b(S.reignEnabled) << "\nHealthPercent=" << S.reignPercent
		  << "\nCombatOnly=" << b(S.reignNeedCombat) << "\n\n";
		o << "[Visual]\nGhostBlade=" << b(S.ghostBlade) << "\n";
	}

	void Ini::Reset()
	{
		S = Settings{};
	}
}
