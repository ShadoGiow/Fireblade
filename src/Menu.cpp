#include "Common.h"

#include "SKSEMenuFramework.h"

// F1 menu (SKSE Menu Framework). Labels are plain ASCII on purpose: the framework's default font has no accented glyphs.

namespace ESA::Menu
{
	namespace
	{
		namespace G = ImGuiMCP;

		const G::ImVec4 kGreen{ 0.45f, 0.90f, 0.45f, 1.0f };
		const G::ImVec4 kRed{ 0.95f, 0.45f, 0.40f, 1.0f };
		const G::ImVec4 kYellow{ 0.95f, 0.85f, 0.40f, 1.0f };

		void Tip(const char* a_text)
		{
			if (G::IsItemHovered()) {
				G::SetTooltip("%s", a_text);
			}
		}

		bool Check(const char* a_label, bool& a_value, const char* a_tip = nullptr)
		{
			const bool changed = G::Checkbox(a_label, &a_value);
			if (a_tip) {
				Tip(a_tip);
			}
			if (changed) {
				MarkSettingsDirty();
			}
			return changed;
		}

		bool SliderI(const char* a_label, int& a_value, int a_min, int a_max, const char* a_tip = nullptr, const char* a_fmt = "%d")
		{
			const bool changed = G::SliderInt(a_label, &a_value, a_min, a_max, a_fmt);
			if (a_tip) {
				Tip(a_tip);
			}
			if (changed) {
				MarkSettingsDirty();
			}
			return changed;
		}

		bool SliderF(const char* a_label, float& a_value, float a_min, float a_max, const char* a_tip = nullptr, const char* a_fmt = "%.2f")
		{
			const bool changed = G::SliderFloat(a_label, &a_value, a_min, a_max, a_fmt);
			if (a_tip) {
				Tip(a_tip);
			}
			if (changed) {
				MarkSettingsDirty();
			}
			return changed;
		}

		void State(const char* a_label, bool a_on, const char* a_yes = "SI", const char* a_no = "no")
		{
			G::Text("%s", a_label);
			G::SameLine();
			G::TextColored(a_on ? kGreen : kRed, "%s", a_on ? a_yes : a_no);
		}

		struct PadName
		{
			std::uint32_t bit;
			const char*   name;
		};

		constexpr PadName kPadNames[] = {
			{ Btn::LB, "LB" }, { Btn::RB, "RB" }, { Btn::LT, "LT" }, { Btn::RT, "RT" },
			{ Btn::L3, "L3" }, { Btn::R3, "R3" }, { Btn::A, "A" }, { Btn::B, "B" },
			{ Btn::X, "X" }, { Btn::Y, "Y" }, { Btn::Back, "Back" }, { Btn::Start, "Start" },
			{ Btn::DUp, "Su" }, { Btn::DDown, "Giu" }, { Btn::DLeft, "Sx" }, { Btn::DRight, "Dx" }
		};

		std::string PadString(std::uint32_t a_mask)
		{
			std::string out;
			for (const auto& p : kPadNames) {
				if (a_mask & p.bit) {
					if (!out.empty()) {
						out += "+";
					}
					out += p.name;
				}
			}
			return out.empty() ? std::string("-") : out;
		}

		void ChordEditor()
		{
			int col = 0;
			for (const auto& p : kPadNames) {
				bool on = (S.ultChord & p.bit) != 0;
				std::string label = std::string(p.name) + "##chord" + std::to_string(p.bit);
				if (G::Checkbox(label.c_str(), &on)) {
					if (on) {
						S.ultChord |= p.bit;
					} else {
						S.ultChord &= ~p.bit;
					}
					MarkSettingsDirty();
				}
				if (++col % 4 != 0) {
					G::SameLine();
				}
			}
			if (col % 4 != 0) {
				G::NewLine();
			}
		}

		// ------------------------------------------------------------------------------------ pages
		void __stdcall RenderStatus()
		{
			const auto st = StatusSnapshot();

			G::SeparatorText("Posa");
			State("Mod caricata correttamente:", F.ok, "SI", "NO - plugin dati non trovato (controlla TargetPlugin nell'INI)");
			State("Posa attiva (le animazioni speciali si attivano):", st.stance);
			G::Text("Mano destra: %s", st.rightName.empty() ? "(niente)" : st.rightName.c_str());
			State("  e' l'arma della mod:", st.bladeEquipped);
			G::Text("Mano sinistra: %s", st.leftName.empty() ? "(niente)" : st.leftName.c_str());
			State("  e' una magia:", st.leftIsSpell);
			State("  e' una magia di fuoco:", st.leftIsFire);

			G::SeparatorText("Marker (letti da Open Animation Replacer)");
			for (int i = 0; i < kMarkerCount; ++i) {
				G::Text("%-16s", kMarkerNames[i]);
				G::SameLine();
				G::TextColored(st.applied[i] ? kGreen : kRed, "%s", st.applied[i] ? "ACCESO" : "spento");
				if (st.desired[i] != st.applied[i]) {
					G::SameLine();
					G::TextColored(kYellow, "(in allineamento)");
				}
			}

			G::SeparatorText("Comandi");
			G::Text("Pulsanti premuti: %s", PadString(st.padHeld).c_str());
			G::Text("Stick sinistro: x=%.2f  y=%.2f", st.lsx, st.lsy);
			State("Ultimate (combinazione):", st.ultHeld, "PREMUTA", "no");
			State("Avanti (combo):", st.fwdHeld, "PREMUTO", "no");
			State("Indietro (esecuzione):", st.backHeld, "PREMUTO", "no");

			G::SeparatorText("Personaggio");
			G::Text("Magicka: %.0f", st.magicka);
			G::Text("Salute: %.0f%%", st.healthPct);
			State("Re-Ignition in ricarica:", st.reignCooldown, "SI", "no");

			G::SeparatorText("Strumenti di prova");
			if (G::Button("Rimuovi tutti i marker")) {
				RequestAction(Action::RemoveMarkers);
			}
			G::SameLine();
			if (G::Button("Risincronizza")) {
				RequestAction(Action::Resync);
			}
			if (G::Button("Prova Re-Ignition")) {
				RequestAction(Action::TestReignition);
			}
			G::SameLine();
			if (G::Button("Prova inseguimento")) {
				RequestAction(Action::TestChase);
			}
			if (G::Button("Prova incenerimento")) {
				RequestAction(Action::TestIncinerate);
			}
			G::SameLine();
			if (G::Button("Prova esplosione")) {
				RequestAction(Action::TestBurst);
			}
		}

		void __stdcall RenderSettings()
		{
			if (G::CollapsingHeader("Generale", G::ImGuiTreeNodeFlags_DefaultOpen)) {
				Check("Mod attiva", S.enabled, "Se spento nessun marker viene acceso e le animazioni speciali restano ferme.");
				Check("Log dettagliato", S.verbose, "Scrive nel log ogni cambio di marker (utile per i test).");
			}

			if (G::CollapsingHeader("Posa (quando si attivano le animazioni)", G::ImGuiTreeNodeFlags_DefaultOpen)) {
				int mode = S.stanceMode;
				if (G::Combo("Condizione##stance", &mode,
						"Spada a destra + magia di fuoco a sinistra\0"
						"Spada a destra + qualsiasi magia di Distruzione\0"
						"Spada a destra + qualsiasi magia\0"
						"Basta la spada a destra\0\0")) {
					S.stanceMode = mode;
					MarkSettingsDirty();
				}
				Tip("Senza dual wielding la spada a una mano sta a destra; la mano sinistra deve avere la magia scelta qui.");
			}

			if (G::CollapsingHeader("Comandi con il pad", G::ImGuiTreeNodeFlags_DefaultOpen)) {
				Check("Comandi pad attivi", S.padEnabled);
				G::Text("Ultimate = tieni premuti (combinazione attuale: %s):", PadString(S.ultChord).c_str());
				ChordEditor();
				Check("Ultimate solo con lo stick fermo", S.ultNeutralStick,
					"L'originale usava A+D insieme, cioe' nessuna direzione netta avanti/indietro.");
				SliderF("Zona morta stick##dz", S.deadzone, 0.10f, 0.90f, "Oltre questa soglia lo stick conta come avanti / indietro.");
				Check("Stick avanti = combo", S.padForwardComb, "Sostituisce il tasto W dell'originale.");
				Check("Stick indietro = esecuzione", S.padBackExec, "Sostituisce il tasto S dell'originale.");
				Check("Tastiera di riserva (W / S / A+D)", S.kbFallback, "Solo se vuoi poter provare anche da tastiera.");
			}

			if (G::CollapsingHeader("Soglie di magicka")) {
				G::TextWrapped("0 = nessun requisito. L'originale non applicava davvero queste soglie: lascia 0 per lo stesso comportamento.");
				SliderI("Abilita' normali##mag1", S.magSkill, 0, 1000, "Magicka minima per le abilita' base.");
				SliderI("Abilita' ULT##mag2", S.magUltSkill, 0, 1000, "Magicka minima per le abilita' collegate all'ultimate.");
				SliderI("Ultimate##mag3", S.magUlt, 0, 1000, "Magicka minima per l'ultimate.");
			}

			if (G::CollapsingHeader("Inseguimento (dopo la catena di marchi)")) {
				Check("Scatto automatico", S.chaseAuto, "Dopo il terzo marchio il personaggio parte da solo verso il bersaglio.");
				SliderF("Ritardo (s)##chdelay", S.chaseDelay, 0.0f, 3.0f);
			}

			if (G::CollapsingHeader("Incenerimento")) {
				Check("Attivo", S.incEnabled);
				SliderI("Lanci extra##incx", S.incExtra, 0, 6, "Quanti lanci in piu' del primo.");
				SliderF("Distanza iniziale##incd", S.incDist, 40.0f, 400.0f, nullptr, "%.0f");
				SliderF("Passo##incs", S.incStep, 0.0f, 120.0f, "Distanza aggiunta a ogni lancio successivo.", "%.0f");
				SliderF("Intervallo (s)##incw", S.incDelay, 0.0f, 0.50f);
			}

			if (G::CollapsingHeader("Esplosione di fuoco")) {
				Check("Attiva", S.burstEnabled, "Esplosione sul bersaglio colpito dal finale dell'attacco.");
			}

			if (G::CollapsingHeader("Re-Ignition (rinascita)")) {
				Check("Attiva", S.reignEnabled, "Cura di emergenza sotto una soglia di salute (ricarica lunga).");
				SliderI("Sotto il (% salute)##rp", S.reignPercent, 1, 60, nullptr, "%d%%");
				Check("Solo in combattimento", S.reignNeedCombat);
			}

			if (G::CollapsingHeader("Aspetto")) {
				Check("Lama fantasma", S.ghostBlade, "Spegni se l'effetto visivo ti disturba.");
			}

			G::Spacing();
			G::Separator();
			if (G::Button("Salva ora")) {
				MarkSettingsDirty();
			}
			G::SameLine();
			if (G::Button("Ripristina predefiniti")) {
				Ini::Reset();
				MarkSettingsDirty();
			}
			G::TextDisabled("Le impostazioni si salvano da sole in Data/SKSE/Plugins/FireBladeStandalone.ini");
		}

		void __stdcall RenderLog()
		{
			G::TextDisabled("Ultimi messaggi del plugin (il file completo e' FireBladeStandalone.log in Documents/My Games/Skyrim Special Edition/SKSE)");
			G::Separator();
			if (G::BeginChild("##fb_log", G::ImVec2{ 0.0f, 360.0f })) {
				const auto lines = LogSnapshot();
				for (const auto& l : lines) {
					G::TextWrapped("%s", l.c_str());
				}
				if (!lines.empty() && G::GetScrollY() >= G::GetScrollMaxY() - 4.0f) {
					G::SetScrollHereY(1.0f);
				}
			}
			G::EndChild();
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			Logf("SKSE Menu Framework non trovato: il menu F1 non sara' disponibile (le impostazioni restano nell'INI)");
			return;
		}
		SKSEMenuFramework::SetSection("Fire Blade Standalone");
		SKSEMenuFramework::AddSectionItem("Stato", RenderStatus);
		SKSEMenuFramework::AddSectionItem("Impostazioni", RenderSettings);
		SKSEMenuFramework::AddSectionItem("Log", RenderLog);
		Logf("menu F1 registrato");
	}
}
