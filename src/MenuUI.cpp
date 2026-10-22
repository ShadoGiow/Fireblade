#include "MenuUI.h"

#include "Runtime.h"
#include "Strings.h"

namespace sm::ui
{
	namespace
	{
		// Layout-compatible with ImVec2 / ImVec4. Passed by value exactly like the C API does.
		struct Vec2
		{
			float x;
			float y;
		};
		struct Vec4
		{
			float x;
			float y;
			float z;
			float w;
		};

		using RenderFn = void(__stdcall*)();

		struct Api
		{
			void (*Text)(const char*, ...) = nullptr;
			void (*TextWrapped)(const char*, ...) = nullptr;
			void (*TextDisabled)(const char*, ...) = nullptr;
			void (*TextColored)(Vec4, const char*, ...) = nullptr;
			bool (*Button)(const char*, Vec2) = nullptr;
			void (*SameLine)(float, float) = nullptr;
			bool (*Checkbox)(const char*, bool*) = nullptr;
			bool (*SliderInt)(const char*, int*, int, int, const char*, int) = nullptr;
			bool (*BeginCombo)(const char*, const char*, int) = nullptr;
			void (*EndCombo)() = nullptr;
			bool (*Selectable)(const char*, bool, int, Vec2) = nullptr;
			void (*Separator)() = nullptr;
			void (*Spacing)() = nullptr;
			void (*SeparatorText)(const char*) = nullptr;
			void (*PushID)(const char*) = nullptr;
			void (*PopID)() = nullptr;
			bool (*IsItemHovered)(int) = nullptr;
			void (*SetTooltip)(const char*, ...) = nullptr;
			void (*BeginDisabled)(bool) = nullptr;
			void (*EndDisabled)() = nullptr;
			void (*SetNextItemWidth)(float) = nullptr;
			void (*AddSectionItem)(const char*, RenderFn) = nullptr;
		};

		Api g;

		template <class Fn>
		bool Bind(Fn& a_out, REX::W32::HMODULE a_module, const char* a_name)
		{
			a_out = reinterpret_cast<Fn>(REX::W32::GetProcAddress(a_module, a_name));
			if (!a_out) {
				logs::error("SKSE Menu Framework does not export '{}' (version 2.x or newer is required)", a_name);
				return false;
			}
			return true;
		}

		constexpr Vec2 kAuto{ 0.0f, 0.0f };
		constexpr Vec4 kGreen{ 0.45f, 0.90f, 0.45f, 1.0f };
		constexpr Vec4 kRed{ 0.95f, 0.45f, 0.40f, 1.0f };
		constexpr Vec4 kYellow{ 1.00f, 0.85f, 0.35f, 1.0f };

		// ---- tiny helpers (everything goes through "%s" so user text can never be taken as a format) ------
		void Text(const std::string& a_text) { g.Text("%s", a_text.c_str()); }
		void Wrapped(const char* a_text) { g.TextWrapped("%s", a_text); }
		void Dim(const char* a_text) { g.TextDisabled("%s", a_text); }
		void Colored(Vec4 a_color, const char* a_text) { g.TextColored(a_color, "%s", a_text); }
		void Tip(const char* a_text)
		{
			if (g.IsItemHovered(0)) {
				g.SetTooltip("%s", a_text);
			}
		}

		const char* TriggerLabel(const Strings& S, Trigger a_trigger)
		{
			switch (a_trigger) {
			case Trigger::Press:
				return S.trigPress;
			case Trigger::Tap:
				return S.trigTap;
			case Trigger::Hold:
				return S.trigHold;
			case Trigger::DoubleTap:
				return S.trigDouble;
			}
			return "?";
		}

		const char* TimeLabel(const Strings& S, Trigger a_trigger)
		{
			switch (a_trigger) {
			case Trigger::Tap:
				return S.timeTap;
			case Trigger::DoubleTap:
				return S.timeDouble;
			default:
				return S.timeHold;
			}
		}

		// "Rescan" feedback: render thread only
		std::uint64_t g_flashUntilMs = 0;
		std::size_t   g_flashPacks = 0;

		std::uint64_t NowMs()
		{
			using namespace std::chrono;
			return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
		}

		// =============================================================================================
		// page: Moveset
		// =============================================================================================
		void __stdcall RenderMoveset()
		{
			auto&       runtime = Runtime::Get();
			const auto  state = runtime.Snapshot();
			const auto& S = GetStrings(state.config.options.language);

			// --- status
			Text(std::string(S.status) + ": ");
			g.SameLine(0.0f, 0.0f);
			if (state.modeOn) {
				Colored(kGreen, S.modeOn);
				for (const auto& pack : state.packs) {
					if (pack.id == state.activePackId) {
						g.SameLine(0.0f, 8.0f);
						Text("- " + pack.name);
						break;
					}
				}
			} else {
				Dim(S.modeOff);
			}

			if (state.modeOn) {
				if (g.Button(S.exitNow, kAuto)) {
					runtime.RequestExit();
				}
			} else if (!state.packs.empty()) {
				if (g.Button(S.enterNow, kAuto)) {
					runtime.RequestEnter();
				}
			}
			Dim(S.menuHintGameplay);

			// --- Open Animation Replacer
			g.Spacing();
			Colored(state.oarOk ? kGreen : kRed, state.oarOk ? S.oarOk : S.oarMissing);

			// --- packs
			g.Spacing();
			g.SeparatorText(S.packs);
			if (state.packs.empty()) {
				Wrapped(S.noPacks);
				Dim(S.noPacksHint);
			}
			const PackInfo* selected = nullptr;
			for (const auto& pack : state.packs) {
				const bool isSelected = pack.id == state.config.selectedPack;
				if (isSelected) {
					selected = &pack;
				}
				std::string label = pack.name;
				if (state.modeOn && pack.id == state.activePackId) {
					label += "  [*]";
				}
				label += "##pack_" + pack.id;
				if (g.Selectable(label.c_str(), isSelected, 0, kAuto)) {
					runtime.RequestSelectPack(pack.id);
				}
				if (!pack.description.empty()) {
					Tip(pack.description.c_str());
				}
			}

			if (selected) {
				g.Spacing();
				if (!selected->author.empty()) {
					Dim(selected->author.c_str());
				}
				if (!selected->description.empty()) {
					Wrapped(selected->description.c_str());
				}
				if (!selected->notes.empty()) {
					g.Spacing();
					Dim(S.packNotes);
					Wrapped(selected->notes.c_str());
				}
			}

			// --- rescan
			g.Spacing();
			if (g.Button(S.rescan, kAuto)) {
				g_flashPacks = runtime.RescanPacks();
				g_flashUntilMs = NowMs() + 3000;
			}
			if (NowMs() < g_flashUntilMs) {
				g.SameLine(0.0f, 8.0f);
				Text(std::string(S.rescanned) + " (" + std::to_string(g_flashPacks) + ")");
			}

			// --- problems found while loading
			if (!state.warnings.empty()) {
				g.Spacing();
				g.Separator();
				for (const auto& warning : state.warnings) {
					Colored(kYellow, warning.c_str());
				}
			}
		}

		// =============================================================================================
		// page: Controls
		// =============================================================================================
		const char* SlotLabel(const Strings& S, Action a_action, bool a_toggle)
		{
			switch (a_action) {
			case Action::Enter:
				return a_toggle ? S.actionEnterToggle : S.actionEnter;
			case Action::Exit:
				return S.actionExit;
			case Action::NextPack:
				return S.actionNext;
			case Action::PrevPack:
				return S.actionPrev;
			default:
				return "?";
			}
		}

		void DrawSlot(Runtime& a_runtime, const UiState& a_state, const Strings& S, Action a_action)
		{
			const auto  idx = static_cast<std::size_t>(a_action);
			const auto& binding = a_state.config.bindings[idx];
			const bool  toggle = a_state.config.options.toggleWithEnter;

			g.PushID(std::string(ActionToken(a_action)).c_str());
			g.SeparatorText(SlotLabel(S, a_action, toggle));

			if (a_action == Action::Exit && toggle) {
				Dim(S.exitUnused);
				g.PopID();
				return;
			}

			bool enabled = binding.enabled;
			if (g.Checkbox(S.enabled, &enabled)) {
				a_runtime.Edit([idx, enabled](Config& c) { c.bindings[idx].enabled = enabled; });
			}

			g.SameLine(0.0f, 16.0f);
			const bool capturingThis = a_state.captureActive && a_state.captureSlot == static_cast<int>(idx);
			if (capturingThis) {
				const std::string live = a_state.captureLive.empty() ? "..." : a_state.captureLive;
				Colored(kYellow, (std::string(S.capturingNow) + ": " + live).c_str());
			} else if (binding.inputs.empty()) {
				Dim(S.notAssigned);
			} else {
				Text(DescribeChord(binding.inputs));
			}

			if (g.Button(S.assign, kAuto)) {
				a_runtime.BeginCapture(a_action);
			}
			g.SameLine(0.0f, 8.0f);
			if (g.Button(S.clear, kAuto)) {
				a_runtime.Edit([idx](Config& c) { c.bindings[idx].inputs.clear(); });
			}

			// --- press type
			g.SetNextItemWidth(220.0f);
			if (g.BeginCombo(S.trigger, TriggerLabel(S, binding.trigger), 0)) {
				for (const auto t : { Trigger::Press, Trigger::Tap, Trigger::Hold, Trigger::DoubleTap }) {
					if (g.Selectable(TriggerLabel(S, t), t == binding.trigger, 0, kAuto)) {
						a_runtime.Edit([idx, t](Config& c) { c.bindings[idx].trigger = t; });
					}
				}
				g.EndCombo();
			}

			if (binding.trigger != Trigger::Press) {
				int ms = static_cast<int>(binding.EffectiveMs());
				g.SetNextItemWidth(220.0f);
				const std::string label = std::string(TimeLabel(S, binding.trigger)) + "##ms";
				if (g.SliderInt(label.c_str(), &ms, static_cast<int>(kMinTimeMs), static_cast<int>(kMaxTimeMs), "%d ms", 0)) {
					const auto value = static_cast<std::uint32_t>(ms);
					a_runtime.Edit([idx, value](Config& c) { c.bindings[idx].timeMs = value; });
				}
			}

			g.PopID();
		}

		void __stdcall RenderControls()
		{
			auto&       runtime = Runtime::Get();
			const auto  state = runtime.Snapshot();
			const auto& S = GetStrings(state.config.options.language);

			Wrapped(S.controlsIntro);
			g.Spacing();

			if (state.captureActive) {
				Colored(kYellow, S.capturing);
				if (g.Button(S.cancel, kAuto)) {
					runtime.CancelCapture();
				}
				g.Spacing();
			}

			// While a capture is running the rest is greyed out: a pad button pressed to be recorded must not
			// also activate a widget of this very menu.
			g.BeginDisabled(state.captureActive);

			bool toggle = state.config.options.toggleWithEnter;
			if (g.Checkbox(S.toggleWithEnter, &toggle)) {
				runtime.Edit([toggle](Config& c) { c.options.toggleWithEnter = toggle; });
			}
			Tip(S.toggleWithEnterTip);

			for (const auto action : { Action::Enter, Action::Exit, Action::NextPack, Action::PrevPack }) {
				DrawSlot(runtime, state, S, action);
			}

			g.EndDisabled();
		}

		// =============================================================================================
		// page: Options
		// =============================================================================================
		void Option(Runtime& a_runtime, const char* a_label, const char* a_tip, bool a_value, void (*a_apply)(Options&, bool))
		{
			bool value = a_value;
			if (g.Checkbox(a_label, &value)) {
				a_runtime.Edit([a_apply, value](Config& c) { a_apply(c.options, value); });
			}
			if (a_tip && *a_tip) {
				Tip(a_tip);
			}
		}

		void __stdcall RenderOptions()
		{
			auto&       runtime = Runtime::Get();
			const auto  state = runtime.Snapshot();
			const auto& S = GetStrings(state.config.options.language);
			const auto& o = state.config.options;

			Option(runtime, S.optNotify, nullptr, o.notify, [](Options& x, bool v) { x.notify = v; });
			Option(runtime, S.optRefresh, S.optRefreshTip, o.refreshOnSwap, [](Options& x, bool v) { x.refreshOnSwap = v; });
			Option(runtime, S.optAutoExit, S.optAutoExitTip, o.autoExitOnSheathe, [](Options& x, bool v) { x.autoExitOnSheathe = v; });
			Option(runtime, S.optBlockAttack, S.optBlockAttackTip, o.blockWhileAttacking, [](Options& x, bool v) { x.blockWhileAttacking = v; });

			g.Spacing();
			g.SetNextItemWidth(220.0f);
			const char* current = o.language == "en" ? "English" : "Italiano";
			if (g.BeginCombo(S.optLanguage, current, 0)) {
				if (g.Selectable("Italiano", o.language != "en", 0, kAuto)) {
					runtime.Edit([](Config& c) { c.options.language = "it"; });
				}
				if (g.Selectable("English", o.language == "en", 0, kAuto)) {
					runtime.Edit([](Config& c) { c.options.language = "en"; });
				}
				g.EndCombo();
			}

			g.Spacing();
			Colored(state.oarOk ? kGreen : kRed, state.oarOk ? S.oarOk : S.oarMissing);
			Dim(state.oarStatus.c_str());
			g.Spacing();
			Dim(S.configFile);
		}
	}

	bool Register()
	{
		const auto module = REX::W32::GetModuleHandleA("SKSEMenuFramework.dll");
		if (!module) {
			logs::warn("SKSE Menu Framework is not installed: no menu section. Edit SwapMoveset.json instead.");
			return false;
		}

		bool ok = true;
		ok &= Bind(g.Text, module, "igText");
		ok &= Bind(g.TextWrapped, module, "igTextWrapped");
		ok &= Bind(g.TextDisabled, module, "igTextDisabled");
		ok &= Bind(g.TextColored, module, "igTextColored");
		ok &= Bind(g.Button, module, "igButton");
		ok &= Bind(g.SameLine, module, "igSameLine");
		ok &= Bind(g.Checkbox, module, "igCheckbox");
		ok &= Bind(g.SliderInt, module, "igSliderInt");
		ok &= Bind(g.BeginCombo, module, "igBeginCombo");
		ok &= Bind(g.EndCombo, module, "igEndCombo");
		ok &= Bind(g.Selectable, module, "igSelectable_Bool");
		ok &= Bind(g.Separator, module, "igSeparator");
		ok &= Bind(g.Spacing, module, "igSpacing");
		ok &= Bind(g.SeparatorText, module, "igSeparatorText");
		ok &= Bind(g.PushID, module, "igPushID_Str");
		ok &= Bind(g.PopID, module, "igPopID");
		ok &= Bind(g.IsItemHovered, module, "igIsItemHovered");
		ok &= Bind(g.SetTooltip, module, "igSetTooltip");
		ok &= Bind(g.BeginDisabled, module, "igBeginDisabled");
		ok &= Bind(g.EndDisabled, module, "igEndDisabled");
		ok &= Bind(g.SetNextItemWidth, module, "igSetNextItemWidth");
		ok &= Bind(g.AddSectionItem, module, "AddSectionItem");
		if (!ok) {
			g = Api{};  // never call half a table
			logs::error("SKSE Menu Framework is too old or incompatible: no menu section.");
			return false;
		}

		const auto& S = GetStrings(Runtime::Get().Language());
		const std::string base = std::string(S.section) + "/";
		g.AddSectionItem((base + S.pageMoveset).c_str(), RenderMoveset);
		g.AddSectionItem((base + S.pageControls).c_str(), RenderControls);
		g.AddSectionItem((base + S.pageOptions).c_str(), RenderOptions);
		logs::info("menu section '{}' registered", S.section);
		return true;
	}
}
