// Runs the plugin's own game-side code (Runtime, InputPoller, MenuUI, main.cpp) against a *fake game*:
//  - tests/stubs/ replaces CommonLibSSE-NG with a handful of classes,
//  - the Win32 / XInput / ImGui entry points the plugin resolves at run time are faked below.
// It exercises the real poll thread, locking, binding engine, key capture, saving, mode switching and the
// three menu pages. It cannot prove the code matches the real CommonLib (that is checked by the CI build),
// but it does prove the logic and the threading.
//
//   tests/run_runtime_tests.sh            (add SANITIZE=thread to hunt for data races)

#include "ActivePack.h"
#include "MenuUI.h"
#include "OarRegistration.h"
#include "Runtime.h"
#include "core/Hash.h"

#include <nlohmann/json.hpp>

#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

using namespace sm;

// ------------------------------------------------------------------------------------------------------
// tiny test harness
// ------------------------------------------------------------------------------------------------------
static int g_failures = 0;
static int g_checks = 0;

#define CHECK(...)                                                                                      \
	do {                                                                                                \
		++g_checks;                                                                                     \
		if (!(__VA_ARGS__)) {                                                                           \
			++g_failures;                                                                               \
			std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #__VA_ARGS__ << std::endl;   \
		}                                                                                               \
	} while (0)

// ------------------------------------------------------------------------------------------------------
// fake task queue / messaging (the "game main thread" is whoever calls Pump())
// ------------------------------------------------------------------------------------------------------
static std::mutex                        g_taskLock;
static std::vector<std::function<void()>> g_tasks;
static SKSE::MessagingInterface::EventCallback* g_listener = nullptr;

void SKSE::TaskInterface::AddTask(std::function<void()> a_task) const
{
	std::lock_guard lock(g_taskLock);
	g_tasks.push_back(std::move(a_task));
}

bool SKSE::MessagingInterface::RegisterListener(EventCallback* a_handler) const
{
	g_listener = a_handler;
	return true;
}

static void Dispatch(std::uint32_t a_type)
{
	SKSE::MessagingInterface::Message msg{ "SKSE", a_type, 0, nullptr };
	g_listener(&msg);
}

static void RunTasks()
{
	std::vector<std::function<void()>> batch;
	{
		std::lock_guard lock(g_taskLock);
		batch.swap(g_tasks);
	}
	for (auto& t : batch) {
		t();
	}
}

/// Lets real time pass (the poll thread works in real time) while running queued game-thread tasks.
static void Pump(int a_ms)
{
	const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(a_ms);
	while (std::chrono::steady_clock::now() < end) {
		RunTasks();
		std::this_thread::sleep_for(std::chrono::milliseconds(4));
	}
	RunTasks();
}

/// Pump() while the menu "renders" every 40 ms (that is what marks it as open).
static void PumpWithMenu(int a_ms)
{
	const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(a_ms);
	while (std::chrono::steady_clock::now() < end) {
		(void)Runtime::Get().Snapshot();
		Pump(40);
	}
}

// ------------------------------------------------------------------------------------------------------
// fake Win32 / XInput
// ------------------------------------------------------------------------------------------------------
namespace fakewin
{
	std::atomic<bool>          keys[256];
	std::atomic<std::uint16_t> padButtons{ 0 };
	std::atomic<std::uint8_t>  leftTrigger{ 0 };
	std::atomic<std::uint8_t>  rightTrigger{ 0 };
	std::atomic<bool>          padConnected{ true };
	std::atomic<void*>         foreground{ nullptr };

	std::int16_t GetAsyncKeyState(int a_vk) { return keys[a_vk & 0xFF].load() ? static_cast<std::int16_t>(0x8000) : 0; }
	void*        GetForegroundWindow() { return foreground.load(); }
	std::uint32_t XInputGetState(std::uint32_t a_user, void* a_state)
	{
		if (a_user != 0 || !padConnected.load()) {
			return 1167;  // ERROR_DEVICE_NOT_CONNECTED
		}
		auto* s = static_cast<REX::W32::XINPUT_STATE*>(a_state);
		*s = {};
		s->gamepad.buttons = padButtons.load();
		s->gamepad.leftTrigger = leftTrigger.load();
		s->gamepad.rightTrigger = rightTrigger.load();
		return 0;
	}

	void ReleaseAll()
	{
		for (auto& k : keys) {
			k = false;
		}
		padButtons = 0;
		leftTrigger = 0;
		rightTrigger = 0;
	}
}

// ------------------------------------------------------------------------------------------------------
// fake ImGui (cimgui) - records what the pages draw, lets the test "click" things
// ------------------------------------------------------------------------------------------------------
namespace fakeimgui
{
	struct Vec2
	{
		float x, y;
	};
	struct Vec4
	{
		float x, y, z, w;
	};

	std::vector<std::string> out;       // everything drawn in the current page render
	std::set<std::string>    clicks;    // labels that return true once (buttons, selectables, checkboxes)
	std::set<std::string>    openCombos;
	std::map<std::string, std::function<void()>> sections;

	std::string Fmt(const char* a_fmt, va_list a_args)
	{
		char buf[1024];
		std::vsnprintf(buf, sizeof(buf), a_fmt, a_args);
		return buf;
	}
	void Add(const std::string& a_line) { out.push_back(a_line); }
	bool Click(const std::string& a_label)
	{
		const auto it = clicks.find(a_label);
		if (it == clicks.end()) {
			return false;
		}
		clicks.erase(it);
		return true;
	}

	void Text(const char* f, ...)
	{
		va_list a;
		va_start(a, f);
		Add(Fmt(f, a));
		va_end(a);
	}
	void TextColored(Vec4, const char* f, ...)
	{
		va_list a;
		va_start(a, f);
		Add(Fmt(f, a));
		va_end(a);
	}
	bool Button(const char* l, Vec2)
	{
		Add(std::string("[btn] ") + l);
		return Click(l);
	}
	void SameLine(float, float) {}
	bool Checkbox(const char* l, bool* v)
	{
		Add(std::string("[chk] ") + l + (*v ? " =on" : " =off"));
		if (Click(l)) {
			*v = !*v;
			return true;
		}
		return false;
	}
	bool SliderInt(const char* l, int* v, int lo, int hi, const char*, int)
	{
		Add(std::string("[sld] ") + l + " =" + std::to_string(*v) + " [" + std::to_string(lo) + ".." + std::to_string(hi) + "]");
		if (Click(l)) {
			*v = lo;  // "drag to the minimum"
			return true;
		}
		return false;
	}
	bool BeginCombo(const char* l, const char* preview, int)
	{
		Add(std::string("[cmb] ") + l + " =" + preview);
		return openCombos.count(l) != 0;
	}
	void EndCombo() {}
	bool Selectable(const char* l, bool sel, int, Vec2)
	{
		Add(std::string("[sel] ") + l + (sel ? " *" : ""));
		return Click(l);
	}
	void Separator() {}
	void Spacing() {}
	void SeparatorText(const char* l) { Add(std::string("== ") + l); }
	void PushID(const char*) {}
	void PopID() {}
	bool IsItemHovered(int) { return false; }
	void SetTooltip(const char*, ...) {}
	int  disabledDepth = 0;
	void BeginDisabled(bool d)
	{
		if (d) {
			++disabledDepth;
		}
		Add(d ? "[disabled begin]" : "[disabled off]");
	}
	void EndDisabled() {}
	void SetNextItemWidth(float) {}
	void AddSectionItem(const char* path, void (*fn)()) { sections[path] = fn; }

	std::string Page(const std::string& a_path)
	{
		out.clear();
		sections.at(a_path)();
		std::string all;
		for (const auto& l : out) {
			all += l + "\n";
		}
		return all;
	}
	bool Has(const std::string& a_page, const std::string& a_text) { return a_page.find(a_text) != std::string::npos; }
}

static bool                         g_smfPresent = true;
static std::map<std::string, void*> g_exports;

namespace REX::W32
{
	HMODULE LoadLibraryA(const char* a_name) noexcept
	{
		const std::string n = a_name;
		if (n == "user32.dll") {
			return reinterpret_cast<HMODULE>(std::uintptr_t{ 1 });
		}
		if (n == "xinput1_4.dll") {
			return reinterpret_cast<HMODULE>(std::uintptr_t{ 2 });
		}
		return nullptr;
	}
	HMODULE GetModuleHandleA(const char* a_name) noexcept
	{
		if (std::string(a_name) == "SKSEMenuFramework.dll" && g_smfPresent) {
			return reinterpret_cast<HMODULE>(std::uintptr_t{ 3 });
		}
		return nullptr;
	}
	void* GetProcAddress(HMODULE, const char* a_name) noexcept
	{
		const auto it = g_exports.find(a_name);
		return it != g_exports.end() ? it->second : nullptr;
	}
}

// OAR registration lives in PackActiveCondition.cpp (needs the real OAR headers): stubbed here.
std::string sm::RegisterOarCondition(bool& a_ok)
{
	a_ok = true;
	return "registered (fake)";
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface*);  // main.cpp

static void FillExports()
{
	using namespace fakeimgui;
	auto put = [](const char* n, auto f) { g_exports[n] = reinterpret_cast<void*>(f); };
	put("GetAsyncKeyState", &fakewin::GetAsyncKeyState);
	put("GetForegroundWindow", &fakewin::GetForegroundWindow);
	put("XInputGetState", &fakewin::XInputGetState);
	put("igText", static_cast<void (*)(const char*, ...)>(&Text));
	put("igTextWrapped", static_cast<void (*)(const char*, ...)>(&Text));
	put("igTextDisabled", static_cast<void (*)(const char*, ...)>(&Text));
	put("igTextColored", static_cast<void (*)(Vec4, const char*, ...)>(&TextColored));
	put("igButton", &Button);
	put("igSameLine", &SameLine);
	put("igCheckbox", &Checkbox);
	put("igSliderInt", &SliderInt);
	put("igBeginCombo", &BeginCombo);
	put("igEndCombo", &EndCombo);
	put("igSelectable_Bool", &Selectable);
	put("igSeparator", &Separator);
	put("igSpacing", &Spacing);
	put("igSeparatorText", &SeparatorText);
	put("igPushID_Str", &PushID);
	put("igPopID", &PopID);
	put("igIsItemHovered", &IsItemHovered);
	put("igSetTooltip", static_cast<void (*)(const char*, ...)>(&SetTooltip));
	put("igBeginDisabled", &BeginDisabled);
	put("igEndDisabled", &EndDisabled);
	put("igSetNextItemWidth", &SetNextItemWidth);
	put("AddSectionItem", &AddSectionItem);
}

// ------------------------------------------------------------------------------------------------------
// helpers
// ------------------------------------------------------------------------------------------------------
namespace pad = sm::pad;

static void PadDown(std::uint32_t a_mask) { fakewin::padButtons = static_cast<std::uint16_t>(fakewin::padButtons.load() | a_mask); }
static void PadUp(std::uint32_t a_mask) { fakewin::padButtons = static_cast<std::uint16_t>(fakewin::padButtons.load() & ~a_mask); }

static bool HudHas(const std::string& a_text)
{
	for (const auto& m : fake::game.hud) {
		if (m.find(a_text) != std::string::npos) {
			return true;
		}
	}
	return false;
}

static std::size_t CountGraphKicks()
{
	std::size_t n = 0;
	for (const auto& e : fake::game.graphEvents) {
		n += (e == "IdleForceDefaultState");
	}
	return n;
}

static void WriteFile(const std::filesystem::path& a_path, const std::string& a_text)
{
	std::filesystem::create_directories(a_path.parent_path());
	std::ofstream(a_path, std::ios::binary) << a_text;
}

static Binding MakeBinding(Trigger a_trigger, std::vector<InputId> a_inputs, std::uint32_t a_ms = 0)
{
	Binding b;
	b.enabled = true;
	b.trigger = a_trigger;
	b.timeMs = a_ms;
	b.inputs = std::move(a_inputs);
	return b;
}

static const InputId kLB{ Device::Gamepad, pad::kLB };
static const InputId kKeyG{ Device::Keyboard, 'G' };
static const InputId kKeyH{ Device::Keyboard, 'H' };

static std::uint64_t HashOf(const char* a_id) { return HashId(a_id); }

// ------------------------------------------------------------------------------------------------------
int main()
{
	SKSE::log::Quiet = true;

	char tmpl[] = "/tmp/swapmoveset_rt_XXXXXX";
	const char* tmp = mkdtemp(tmpl);
	if (!tmp) {
		std::cerr << "mkdtemp failed\n";
		return 2;
	}
	std::filesystem::current_path(tmp);
	const std::filesystem::path dir = std::filesystem::path("Data") / "SKSE" / "Plugins" / "SwapMoveset";

	// two packs, listed out of order on purpose
	WriteFile(dir / "Packs" / "b.json", R"({"id":"other","name":"Other Pack","author":"me","order":20})");
	WriteFile(dir / "Packs" / "a.json", R"({"id":"demo","name":"Demo Pack","author":"me","description":"first","notes":"needs MCO","order":10})");
	WriteFile(dir / "Packs" / "bad.json", R"({"name":"no id"})");
	// a config that already has bindings, to prove loading works: Enter = hold LB+RB 400 ms
	WriteFile(dir / "SwapMoveset.json",
		R"({"version":1,"language":"it","selectedPack":"","options":{"notify":true,"refreshOnSwap":true,"autoExitOnSheathe":false,"blockWhileAttacking":true,"toggleWithEnter":true},
		"bindings":{"enter":{"enabled":true,"trigger":"hold","timeMs":400,"inputs":["pad:LB","pad:RB"]},
		            "exit":{"enabled":true,"trigger":"press","inputs":[]},
		            "nextPack":{"enabled":true,"trigger":"press","inputs":["pad:DPadRight"]},
		            "prevPack":{"enabled":true,"trigger":"press","inputs":[]}}})");

	FillExports();
	fakewin::foreground = fake::game.window;

	// ---- plugin load + SKSE messages --------------------------------------------------------------------
	CHECK(SKSEPlugin_Load(nullptr));
	CHECK(g_listener != nullptr);
	Dispatch(SKSE::MessagingInterface::kPostLoad);
	Dispatch(SKSE::MessagingInterface::kDataLoaded);
	Pump(50);

	{
		const auto st = Runtime::Get().Snapshot();
		CHECK(st.packs.size() == 2);
		CHECK(st.packs.size() == 2 && st.packs[0].id == "demo" && st.packs[1].id == "other");  // sorted by order
		CHECK(st.config.selectedPack == "demo");  // empty in the file -> first pack
		CHECK(!st.modeOn);
		CHECK(st.oarOk);
		CHECK(st.config.bindings[0].trigger == Trigger::Hold && st.config.bindings[0].timeMs == 400);
		CHECK(st.config.bindings[0].inputs.size() == 2);
		CHECK(!st.warnings.empty());  // bad.json is reported
		CHECK(g_exports.count("igText") == 1);
		CHECK(fakeimgui::sections.size() == 3);
		CHECK(fakeimgui::sections.count("Swap Moveset/Moveset") == 1);
		CHECK(fakeimgui::sections.count("Swap Moveset/Controlli") == 1);
		CHECK(fakeimgui::sections.count("Swap Moveset/Opzioni") == 1);
	}
	CHECK(g_activePackHash.load() == 0);
	Pump(700);  // let the menu-seen marker from Snapshot() expire

	// ---- enter with a long press (held LB+RB for the configured 400 ms) ------------------------------
	fake::game.hud.clear();
	PadDown(pad::kLB);
	Pump(100);
	PadDown(pad::kRB);
	Pump(250);
	CHECK(g_activePackHash.load() == 0);  // not yet: 250 ms < 400 ms
	Pump(350);
	CHECK(g_activePackHash.load() == HashOf("demo"));
	CHECK(HudHas("Moveset attivo: Demo Pack"));
	PadUp(pad::kLB | pad::kRB);
	Pump(350);  // the pending animation kick runs from the 100 ms tick
	CHECK(CountGraphKicks() == 1);
	CHECK(Runtime::Get().Snapshot().modeOn);

	// ---- holding the same chord does not fire again until released and re-held (toggle) --------------
	fake::game.hud.clear();
	Pump(700);
	PadDown(pad::kLB | pad::kRB);
	Pump(700);
	CHECK(g_activePackHash.load() == 0);  // toggled off
	CHECK(HudHas("Moveset disattivato"));
	PadUp(pad::kLB | pad::kRB);
	Pump(350);
	CHECK(CountGraphKicks() == 2);  // exit also refreshes

	// ---- a tap on only one of the two does nothing -----------------------------------------------------
	PadDown(pad::kLB);
	Pump(700);
	PadUp(pad::kLB);
	Pump(100);
	CHECK(g_activePackHash.load() == 0);

	// ---- inputs already held when the chord is completed never fire "by surprise" ---------------------
	// (LB is held from before; RB added later. The chord is complete, so it counts from RB.)
	// Held-before-start is covered by the engine tests; here: hold both through a config change.
	PadDown(pad::kLB | pad::kRB);
	Pump(150);
	Runtime::Get().Edit([](Config& c) { c.options.notify = true; });  // identical bindings: no reset
	Pump(400);
	CHECK(g_activePackHash.load() == HashOf("demo"));  // fired once at 400 ms
	PadUp(pad::kLB | pad::kRB);
	Pump(100);
	Runtime::Get().Edit([](Config& c) {
		c.bindings[0].trigger = Trigger::Hold;
		c.bindings[0].timeMs = 600;
	});
	Pump(100);

	// the mode is on now: leave through the menu button path
	fake::game.hud.clear();
	Runtime::Get().RequestExit();
	Pump(50);
	CHECK(g_activePackHash.load() == 0);
	CHECK(HudHas("Moveset disattivato"));
	Pump(300);

	// ---- while the F1 menu is on screen, bindings stay silent -----------------------------------------
	PadDown(pad::kLB | pad::kRB);
	PumpWithMenu(900);
	CHECK(g_activePackHash.load() == 0);
	PadUp(pad::kLB | pad::kRB);
	Pump(400);  // menu no longer drawn
	PadDown(pad::kLB | pad::kRB);
	Pump(900);
	CHECK(g_activePackHash.load() == HashOf("demo"));
	PadUp(pad::kLB | pad::kRB);
	Pump(100);
	// leave the mode again quietly for the next tests
	Runtime::Get().RequestExit();
	Pump(50);
	CHECK(g_activePackHash.load() == 0);
	Pump(350);

	// ---- gating: attack in progress, frozen game, pause menu, focus -------------------------------------
	fake::game.attack = 2;
	fake::game.hud.clear();
	PadDown(pad::kLB | pad::kRB);
	Pump(900);
	CHECK(g_activePackHash.load() == 0);
	CHECK(HudHas("non adesso"));
	PadUp(pad::kLB | pad::kRB);
	fake::game.attack = 0;
	Pump(100);

	RE::Main::GetSingleton()->GetRuntimeData().freezeTime = true;  // e.g. another SMF page is open
	PadDown(pad::kLB | pad::kRB);
	Pump(900);
	CHECK(g_activePackHash.load() == 0);
	PadUp(pad::kLB | pad::kRB);
	RE::Main::GetSingleton()->GetRuntimeData().freezeTime = false;
	Pump(100);

	fake::game.paused = true;
	PadDown(pad::kLB | pad::kRB);
	Pump(900);
	CHECK(g_activePackHash.load() == 0);
	PadUp(pad::kLB | pad::kRB);
	fake::game.paused = false;
	Pump(100);

	fakewin::foreground = reinterpret_cast<void*>(0x9999);  // alt-tabbed away
	PadDown(pad::kLB | pad::kRB);
	Pump(900);
	CHECK(g_activePackHash.load() == 0);
	fakewin::foreground = fake::game.window;  // back, chord still held: quarantined until released
	Pump(900);
	CHECK(g_activePackHash.load() == 0);
	PadUp(pad::kLB | pad::kRB);
	Pump(100);

	// ---- next / previous pack (press on D-pad right) --------------------------------------------------------
	fake::game.hud.clear();
	PadDown(pad::kRight);
	Pump(80);
	PadUp(pad::kRight);
	Pump(80);
	CHECK(Runtime::Get().Snapshot().config.selectedPack == "other");
	CHECK(HudHas("Pack selezionato: Other Pack"));

	// switching pack while the mode is on takes effect at once
	Runtime::Get().RequestEnter();
	Pump(50);
	CHECK(g_activePackHash.load() == HashOf("other"));
	Runtime::Get().RequestSelectPack("demo");
	Pump(50);
	CHECK(g_activePackHash.load() == HashOf("demo"));
	CHECK(Runtime::Get().Snapshot().activePackId == "demo");
	Pump(300);

	// ---- ResetMode on load / new game --------------------------------------------------------------------------
	Dispatch(SKSE::MessagingInterface::kPreLoadGame);
	CHECK(g_activePackHash.load() == 0);
	CHECK(!Runtime::Get().Snapshot().modeOn);
	Dispatch(SKSE::MessagingInterface::kPostLoadGame);
	Pump(400);  // the Snapshot() above marked the menu as open for a moment

	// ---- separate exit binding (keyboard G enters via press, H exits via double tap) ------------------------------
	Runtime::Get().Edit([](Config& c) {
		c.options.toggleWithEnter = false;
		c.bindings[0] = MakeBinding(Trigger::Press, { kKeyG });
		c.bindings[1] = MakeBinding(Trigger::DoubleTap, { kKeyH }, 400);
	});
	Pump(100);
	fakewin::keys['G'] = true;
	Pump(60);
	fakewin::keys['G'] = false;
	Pump(100);
	CHECK(g_activePackHash.load() == HashOf("demo"));
	// pressing G again must NOT exit (toggle is off)
	fakewin::keys['G'] = true;
	Pump(60);
	fakewin::keys['G'] = false;
	Pump(100);
	CHECK(g_activePackHash.load() == HashOf("demo"));
	// one tap of H: nothing
	fakewin::keys['H'] = true;
	Pump(60);
	fakewin::keys['H'] = false;
	Pump(150);
	CHECK(g_activePackHash.load() == HashOf("demo"));
	// second tap of H inside the interval: exits
	fakewin::keys['H'] = true;
	Pump(60);
	fakewin::keys['H'] = false;
	Pump(100);
	CHECK(g_activePackHash.load() == 0);
	Pump(300);

	// ---- auto exit when the weapon is sheathed --------------------------------------------------------------
	Runtime::Get().Edit([](Config& c) { c.options.autoExitOnSheathe = true; });
	fake::game.weaponDrawn = false;
	Runtime::Get().RequestEnter();
	Pump(400);
	CHECK(g_activePackHash.load() == HashOf("demo"));  // not drawn yet: stays on
	fake::game.weaponDrawn = true;
	Pump(300);
	CHECK(g_activePackHash.load() == HashOf("demo"));
	fake::game.weaponDrawn = false;
	Pump(400);
	CHECK(g_activePackHash.load() == 0);
	Runtime::Get().Edit([](Config& c) { c.options.autoExitOnSheathe = false; });

	// ---- the animation kick is skipped while busy and abandoned after a while ----------------------------------
	{
		const auto before = CountGraphKicks();
		fake::game.ragdoll = true;
		Runtime::Get().RequestEnter();
		Pump(800);
		CHECK(CountGraphKicks() == before);  // ragdoll: no kick
		fake::game.ragdoll = false;
		Pump(300);
		CHECK(CountGraphKicks() == before + 1);  // stood up again within the give-up window
		Runtime::Get().RequestExit();
		Pump(50);
		Pump(300);
	}

	// ---- key capture (what the "Assign" button does), gamepad chord of 3 + cancel + timeout ----------------------
	Runtime::Get().Edit([](Config& c) {
		c.options.toggleWithEnter = true;
		c.bindings[0] = MakeBinding(Trigger::Press, {});
	});
	{
		(void)Runtime::Get().Snapshot();
		PadDown(pad::kA);  // the button used to click "Assign" is still held: must be ignored
		Runtime::Get().BeginCapture(Action::Enter);
		CHECK(Runtime::Get().Snapshot().captureActive);
		Runtime::Get().BeginCapture(Action::NextPack);  // ignored while one is running
		CHECK(Runtime::Get().Snapshot().captureSlot == 0);
		PumpWithMenu(100);
		PadUp(pad::kA);
		PumpWithMenu(100);
		PadDown(pad::kLB);
		PumpWithMenu(60);
		PadDown(pad::kX);
		fakewin::rightTrigger = 255;
		PumpWithMenu(120);
		CHECK(Runtime::Get().Snapshot().captureLive.find("LB") != std::string::npos);
		PadUp(pad::kLB | pad::kX);
		fakewin::rightTrigger = 0;
		PumpWithMenu(200);
		const auto st = Runtime::Get().Snapshot();
		CHECK(!st.captureActive);
		const auto& chord = st.config.bindings[0].inputs;
		CHECK(chord.size() == 3);
		CHECK(chord.size() == 3 && chord[0] == kLB);
		CHECK(st.config.bindings[0].enabled);
		// the capture itself fired nothing
		CHECK(g_activePackHash.load() == 0);
	}
	{
		// cancel through the API
		(void)Runtime::Get().Snapshot();
		Runtime::Get().BeginCapture(Action::Exit);
		Runtime::Get().CancelCapture();
		PumpWithMenu(100);
		const auto st = Runtime::Get().Snapshot();
		CHECK(!st.captureActive);
		CHECK(st.config.bindings[1].inputs == std::vector<InputId>{ kKeyH });  // unchanged

		// cancel with Esc
		Runtime::Get().BeginCapture(Action::Exit);
		PumpWithMenu(60);
		fakewin::keys[0x1B] = true;
		PumpWithMenu(100);
		fakewin::keys[0x1B] = false;
		PumpWithMenu(100);
		CHECK(!Runtime::Get().Snapshot().captureActive);
		CHECK(Runtime::Get().Snapshot().config.bindings[1].inputs == std::vector<InputId>{ kKeyH });

		// the menu is closed in the middle of a capture: it gives up on its own
		(void)Runtime::Get().Snapshot();
		Runtime::Get().BeginCapture(Action::Exit);
		Pump(1800);
		CHECK(!Runtime::Get().Snapshot().captureActive);
	}

	// ---- config is saved shortly after a change, and reads back identically ----------------------------------------
	Pump(1200);
	{
		std::ifstream in(dir / "SwapMoveset.json", std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		Config onDisk;
		std::vector<std::string> warnings;
		CHECK(ParseConfig(ss.str(), onDisk, warnings));
		const auto live = Runtime::Get().Snapshot().config;
		CHECK(onDisk.bindings == live.bindings);
		CHECK(onDisk.options.toggleWithEnter == live.options.toggleWithEnter);
		CHECK(onDisk.selectedPack == live.selectedPack);
		CHECK(onDisk.bindings[0].inputs.size() == 3);
	}

	// ---- menu pages ------------------------------------------------------------------------------------------------------
	{
		auto page = fakeimgui::Page("Swap Moveset/Moveset");
		CHECK(fakeimgui::Has(page, "Stato: "));
		CHECK(fakeimgui::Has(page, "spenta"));
		CHECK(fakeimgui::Has(page, "[btn] Entra ora"));
		CHECK(fakeimgui::Has(page, "Demo Pack"));
		CHECK(fakeimgui::Has(page, "Other Pack"));
		CHECK(fakeimgui::Has(page, "needs MCO"));  // notes of the selected pack
		CHECK(fakeimgui::Has(page, "Open Animation Replacer: collegato"));
		CHECK(fakeimgui::Has(page, "bad.json"));  // warning

		// click a pack, then "Entra ora"
		fakeimgui::clicks.insert("Demo Pack##pack_demo");
		(void)fakeimgui::Page("Swap Moveset/Moveset");
		Pump(50);
		CHECK(Runtime::Get().Snapshot().config.selectedPack == "demo");
		fakeimgui::clicks.insert("Entra ora");
		(void)fakeimgui::Page("Swap Moveset/Moveset");
		Pump(50);
		CHECK(g_activePackHash.load() == HashOf("demo"));
		page = fakeimgui::Page("Swap Moveset/Moveset");
		CHECK(fakeimgui::Has(page, "ATTIVA"));
		CHECK(fakeimgui::Has(page, "[btn] Esci ora"));
		fakeimgui::clicks.insert("Esci ora");
		(void)fakeimgui::Page("Swap Moveset/Moveset");
		Pump(50);
		CHECK(g_activePackHash.load() == 0);

		// rescan: add a pack on disk, press the button
		WriteFile(dir / "Packs" / "c.json", R"({"id":"third","name":"Third Pack","order":30})");
		fakeimgui::clicks.insert("Ricarica pack");
		(void)fakeimgui::Page("Swap Moveset/Moveset");
		page = fakeimgui::Page("Swap Moveset/Moveset");
		CHECK(fakeimgui::Has(page, "Third Pack"));
		CHECK(fakeimgui::Has(page, "Pack ricaricati. (3)"));
	}
	{
		auto page = fakeimgui::Page("Swap Moveset/Controlli");
		CHECK(fakeimgui::Has(page, "== Entra / Esci (toggle)"));
		CHECK(fakeimgui::Has(page, "Non usato finche"));  // exit slot is greyed while toggle is on
		CHECK(fakeimgui::Has(page, "== Pack successivo"));
		CHECK(fakeimgui::Has(page, "== Pack precedente"));
		CHECK(fakeimgui::Has(page, "LB"));

		// untick "same inputs to enter and exit" -> Exit slot becomes editable
		fakeimgui::clicks.insert("Usa gli stessi tasti per entrare e uscire");
		(void)fakeimgui::Page("Swap Moveset/Controlli");
		page = fakeimgui::Page("Swap Moveset/Controlli");
		CHECK(!Runtime::Get().Snapshot().config.options.toggleWithEnter);
		CHECK(fakeimgui::Has(page, "== Esci dalla modalita'"));
		CHECK(!fakeimgui::Has(page, "Non usato finche"));

		// press-type combo + time slider
		fakeimgui::openCombos.insert("Tipo di pressione");
		fakeimgui::clicks.insert("Pressione prolungata");
		(void)fakeimgui::Page("Swap Moveset/Controlli");
		fakeimgui::openCombos.clear();
		fakeimgui::clicks.clear();
		page = fakeimgui::Page("Swap Moveset/Controlli");
		CHECK(Runtime::Get().Snapshot().config.bindings[0].trigger == Trigger::Hold);
		CHECK(fakeimgui::Has(page, "Durata (ms)"));
		fakeimgui::clicks.insert("Durata (ms)##ms");
		(void)fakeimgui::Page("Swap Moveset/Controlli");
		CHECK(Runtime::Get().Snapshot().config.bindings[0].timeMs == kMinTimeMs);

		// Assign button starts a capture; the rest of the page is greyed out; Cancel button stops it
		fakeimgui::clicks.insert("Assegna");
		(void)fakeimgui::Page("Swap Moveset/Controlli");
		CHECK(Runtime::Get().Snapshot().captureActive);
		page = fakeimgui::Page("Swap Moveset/Controlli");
		CHECK(fakeimgui::Has(page, "[disabled begin]"));
		CHECK(fakeimgui::Has(page, "[btn] Annulla"));
		CHECK(fakeimgui::Has(page, "In ascolto"));
		fakeimgui::clicks.insert("Annulla");
		(void)fakeimgui::Page("Swap Moveset/Controlli");
		Pump(100);
		CHECK(!Runtime::Get().Snapshot().captureActive);

		// clear
		fakeimgui::clicks.insert("Cancella");
		(void)fakeimgui::Page("Swap Moveset/Controlli");
		CHECK(Runtime::Get().Snapshot().config.bindings[0].inputs.empty());
		page = fakeimgui::Page("Swap Moveset/Controlli");
		CHECK(fakeimgui::Has(page, "(nessun tasto assegnato)"));
	}
	{
		auto page = fakeimgui::Page("Swap Moveset/Opzioni");
		CHECK(fakeimgui::Has(page, "Aggiorna subito le animazioni dopo il cambio =on"));
		fakeimgui::clicks.insert("Aggiorna subito le animazioni dopo il cambio");
		(void)fakeimgui::Page("Swap Moveset/Opzioni");
		CHECK(!Runtime::Get().Snapshot().config.options.refreshOnSwap);

		fakeimgui::openCombos.insert("Lingua del menu");
		fakeimgui::clicks.insert("English");
		(void)fakeimgui::Page("Swap Moveset/Opzioni");
		fakeimgui::openCombos.clear();
		CHECK(Runtime::Get().Snapshot().config.options.language == "en");
		page = fakeimgui::Page("Swap Moveset/Opzioni");
		CHECK(fakeimgui::Has(page, "Menu language"));
		page = fakeimgui::Page("Swap Moveset/Moveset");
		CHECK(fakeimgui::Has(page, "Status: "));
		// English messages in game, too
		fake::game.hud.clear();
		Runtime::Get().RequestEnter();
		Pump(50);
		CHECK(HudHas("Moveset active: "));
		Runtime::Get().RequestExit();
		Pump(50);
		CHECK(HudHas("Moveset off"));
	}

	// ---- no SKSE Menu Framework installed: no crash, no section ---------------------------------------------------------
	{
		g_smfPresent = false;
		fakeimgui::sections.clear();
		CHECK(!ui::Register());
		CHECK(fakeimgui::sections.empty());
		g_smfPresent = true;
		g_exports.erase("igSeparatorText");  // an older framework without one of the functions
		CHECK(!ui::Register());
		CHECK(fakeimgui::sections.empty());
	}

	std::cout << g_checks << " checks, " << g_failures << " failed" << std::endl;
	std::_Exit(g_failures == 0 ? 0 : 1);  // the poll thread is detached and endless by design
}
