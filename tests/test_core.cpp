// Unit tests for the portable core (no game, no Windows needed).
// Build & run: tests/run_tests.sh

#include "core/Binding.h"
#include "core/BindingEngine.h"
#include "core/Config.h"
#include "core/Hash.h"
#include "core/Input.h"
#include "core/PackManifest.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace sm;

static int g_failures = 0;
static int g_checks = 0;
static std::string g_current;

#define CHECK(...)                                                                                      \
	do {                                                                                                \
		++g_checks;                                                                                     \
		if (!(__VA_ARGS__)) {                                                                           \
			++g_failures;                                                                               \
			std::printf("  FAIL [%s] %s:%d  %s\n", g_current.c_str(), __FILE__, __LINE__, #__VA_ARGS__); \
		}                                                                                               \
	} while (0)

#define TEST(name)                                                    \
	static void name();                                               \
	static struct name##_reg {                                        \
		name##_reg() { Registry().push_back({ #name, &name }); }      \
	} name##_instance;                                                \
	static void name()

struct TestCase
{
	const char* name;
	void (*fn)();
};
static std::vector<TestCase>& Registry()
{
	static std::vector<TestCase> r;
	return r;
}

// ------------------------------------------------------------------ helpers

static InputId Pad(std::uint32_t c) { return { Device::Gamepad, c }; }
static InputId Key(std::uint32_t c) { return { Device::Keyboard, c }; }

static Binding MakeBinding(Trigger t, std::vector<InputId> inputs, std::uint32_t ms = 0)
{
	Binding b;
	b.enabled = true;
	b.trigger = t;
	b.timeMs = ms;
	b.inputs = std::move(inputs);
	return b;
}

struct Rig
{
	BindingEngine engine;
	BindingSet    set{};

	Rig()
	{
		for (auto& b : set) {
			b.enabled = false;
		}
	}
	void Bind(Action a, Binding b) { set[static_cast<std::size_t>(a)] = std::move(b); }
	void Apply() { engine.SetBindings(set); }

	// returns what fired at this instant
	std::vector<Action> At(std::uint64_t t, std::vector<InputId> down)
	{
		std::vector<Action> fired;
		engine.Update(t, down, fired);
		return fired;
	}
};

static bool Fired(const std::vector<Action>& v, Action a)
{
	return std::find(v.begin(), v.end(), a) != v.end();
}

// ------------------------------------------------------------------ hash / input / tokens

TEST(hash_is_case_insensitive_and_nonzero)
{
	CHECK(HashId("My_Pack") == HashId("my_pack"));
	CHECK(HashId("a") != HashId("b"));
	CHECK(HashId("") != 0);
	CHECK(HashId("x") != 0);
}

TEST(input_tokens_round_trip_for_every_pollable_input)
{
	for (const auto& id : PollableInputs()) {
		InputId back;
		const auto token = ToToken(id);
		CHECK(FromToken(token, back));
		CHECK(back == id);
		CHECK(!DisplayName(id).empty());
	}
	CHECK(PollableInputs().size() > 100);
}

TEST(input_token_parsing_is_lenient_and_strict_where_needed)
{
	InputId id;
	CHECK(FromToken("pad:lb", id) && id == Pad(pad::kLB));
	CHECK(FromToken("PAD:RT", id) && id == Pad(pad::kRT));
	CHECK(FromToken("key:g", id) && id == Key('G'));
	CHECK(FromToken("key:LCtrl", id) && id == Key(0xA2));
	CHECK(FromToken("mouse:middle", id) && id == InputId{ Device::Mouse, 4 });
	CHECK(FromToken("pad:#4096", id) && id == Pad(0x1000));  // numeric fallback
	CHECK(!FromToken("pad:", id));
	CHECK(!FromToken("pad:nonsense", id));
	CHECK(!FromToken("joystick:A", id));
	CHECK(!FromToken("noprefix", id));
	CHECK(!FromToken("key:#0", id));
	CHECK(!FromToken("key:#12x", id));
	// unknown code survives a round trip through the numeric fallback
	const InputId odd{ Device::Keyboard, 0xE7 };
	CHECK(FromToken(ToToken(odd), id) && id == odd);
	CHECK(DescribeChord({ Pad(pad::kLB), Pad(pad::kRB), Pad(pad::kA) }) == "LB + RB + A");
	CHECK(IsUnbindable(Key(kVkEscape)));
	CHECK(!IsUnbindable(Key('A')));
}

TEST(binding_defaults)
{
	Binding b;
	b.trigger = Trigger::Hold;
	CHECK(b.EffectiveMs() == kDefaultHoldMs);
	b.trigger = Trigger::Tap;
	CHECK(b.EffectiveMs() == kDefaultTapMs);
	b.trigger = Trigger::DoubleTap;
	CHECK(b.EffectiveMs() == kDefaultDoubleTapMs);
	b.trigger = Trigger::Press;
	CHECK(b.EffectiveMs() == 0);
	b.trigger = Trigger::Hold;
	b.timeMs = 5;
	CHECK(b.EffectiveMs() == kMinTimeMs);
	b.timeMs = 999999;
	CHECK(b.EffectiveMs() == kMaxTimeMs);
	CHECK(!b.IsValid());
	b.inputs = { Pad(pad::kA) };
	CHECK(b.IsValid());
	b.inputs = { Pad(1), Pad(2), Pad(4), Pad(8), Pad(0x10) };
	CHECK(!b.IsValid());
	Trigger t;
	CHECK(TriggerFromToken("HOLD", t) && t == Trigger::Hold);
	CHECK(TriggerFromToken("doubletap", t) && t == Trigger::DoubleTap);
	CHECK(!TriggerFromToken("long", t));
}

// ------------------------------------------------------------------ engine

TEST(press_fires_once_per_chord_completion)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::Press, { Pad(pad::kA) }));
	r.Apply();
	CHECK(r.At(0, {}).empty());  // first update: nothing held
	CHECK(Fired(r.At(10, { Pad(pad::kA) }), Action::Enter));
	CHECK(r.At(20, { Pad(pad::kA) }).empty());  // still held: no repeat
	CHECK(r.At(30, { Pad(pad::kA) }).empty());
	CHECK(r.At(40, {}).empty());
	CHECK(Fired(r.At(50, { Pad(pad::kA) }), Action::Enter));  // pressed again
}

TEST(chord_needs_every_input_in_any_order)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::Press, { Pad(pad::kLB), Pad(pad::kRB) }));
	r.Apply();
	r.At(0, {});
	CHECK(r.At(10, { Pad(pad::kLB) }).empty());
	CHECK(Fired(r.At(20, { Pad(pad::kLB), Pad(pad::kRB) }), Action::Enter));
	r.At(30, {});
	CHECK(r.At(40, { Pad(pad::kRB) }).empty());
	CHECK(Fired(r.At(50, { Pad(pad::kRB), Pad(pad::kLB) }), Action::Enter));  // reversed order
}

TEST(chord_may_mix_devices_and_tolerates_extra_inputs)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::Press, { Key(0xA2) /*LCtrl*/, Pad(pad::kA) }));
	r.Apply();
	r.At(0, {});
	CHECK(r.At(10, { Pad(pad::kA) }).empty());
	CHECK(Fired(r.At(20, { Pad(pad::kA), Key(0xA2), Pad(pad::kX) }), Action::Enter));
}

TEST(hold_fires_once_after_the_time_and_not_before)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::Hold, { Pad(pad::kLB), Pad(pad::kRB) }, 600));
	r.Apply();
	r.At(0, {});
	CHECK(r.At(1000, { Pad(pad::kLB), Pad(pad::kRB) }).empty());  // chord completes at t=1000
	CHECK(r.At(1300, { Pad(pad::kLB), Pad(pad::kRB) }).empty());
	CHECK(r.At(1599, { Pad(pad::kLB), Pad(pad::kRB) }).empty());
	CHECK(Fired(r.At(1600, { Pad(pad::kLB), Pad(pad::kRB) }), Action::Enter));
	CHECK(r.At(2500, { Pad(pad::kLB), Pad(pad::kRB) }).empty());  // fires only once per hold
	CHECK(r.At(2600, {}).empty());
	// a new hold works again
	CHECK(r.At(3000, { Pad(pad::kLB), Pad(pad::kRB) }).empty());
	CHECK(Fired(r.At(3650, { Pad(pad::kLB), Pad(pad::kRB) }), Action::Enter));
}

TEST(hold_released_early_does_nothing_and_breaking_the_chord_resets_the_timer)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::Hold, { Pad(pad::kLB), Pad(pad::kRB) }, 600));
	r.Apply();
	r.At(0, {});
	r.At(100, { Pad(pad::kLB), Pad(pad::kRB) });
	CHECK(r.At(500, { Pad(pad::kLB), Pad(pad::kRB) }).empty());
	CHECK(r.At(550, { Pad(pad::kLB) }).empty());                              // released RB
	CHECK(r.At(600, { Pad(pad::kLB), Pad(pad::kRB) }).empty());               // back: timer restarts at 600
	CHECK(r.At(1100, { Pad(pad::kLB), Pad(pad::kRB) }).empty());
	CHECK(Fired(r.At(1200, { Pad(pad::kLB), Pad(pad::kRB) }), Action::Enter));
}

TEST(hold_with_default_time_and_a_stalled_clock)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::Hold, { Pad(pad::kY) }));  // timeMs 0 -> 600
	r.Apply();
	r.At(0, {});
	r.At(10, { Pad(pad::kY) });
	CHECK(Fired(r.At(5000, { Pad(pad::kY) }), Action::Enter));  // a huge gap still fires, once
	CHECK(r.At(5010, { Pad(pad::kY) }).empty());
}

TEST(tap_fires_on_release_only_when_short)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::Tap, { Pad(pad::kBack) }, 250));
	r.Apply();
	r.At(0, {});
	CHECK(r.At(100, { Pad(pad::kBack) }).empty());
	CHECK(Fired(r.At(300, {}), Action::Enter));  // held 200 ms
	CHECK(r.At(400, { Pad(pad::kBack) }).empty());
	CHECK(r.At(900, {}).empty());  // held 500 ms: too long
}

TEST(double_tap)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::DoubleTap, { Pad(pad::kStart) }, 350));
	r.Apply();
	r.At(0, {});
	r.At(100, { Pad(pad::kStart) });
	CHECK(r.At(160, {}).empty());                                          // first tap released at 160
	CHECK(Fired(r.At(400, { Pad(pad::kStart) }), Action::Enter));          // second press 240 ms later
	CHECK(r.At(450, {}).empty());
	// a third tap alone must not fire (needs a fresh pair)
	CHECK(r.At(600, { Pad(pad::kStart) }).empty());
	CHECK(r.At(650, {}).empty());
	CHECK(Fired(r.At(800, { Pad(pad::kStart) }), Action::Enter));          // ...but the pair 600/800 does
}

TEST(double_tap_too_slow_or_first_press_too_long)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::DoubleTap, { Pad(pad::kStart) }, 350));
	r.Apply();
	r.At(0, {});
	r.At(100, { Pad(pad::kStart) });
	r.At(150, {});
	CHECK(r.At(700, { Pad(pad::kStart) }).empty());  // gap 550 ms > 350
	r.At(720, {});
	// first press held too long
	r.At(1000, { Pad(pad::kStart) });
	r.At(1600, {});
	CHECK(r.At(1700, { Pad(pad::kStart) }).empty());
}

TEST(inputs_already_down_when_the_engine_starts_are_quarantined)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::Press, { Pad(pad::kA) }));
	r.Apply();
	CHECK(r.At(0, { Pad(pad::kA) }).empty());   // A is held at startup (e.g. menu just closed)
	CHECK(r.At(50, { Pad(pad::kA) }).empty());
	CHECK(r.At(60, {}).empty());
	CHECK(Fired(r.At(70, { Pad(pad::kA) }), Action::Enter));  // released then pressed: counts
}

TEST(reset_and_set_bindings_quarantine_held_inputs)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::Press, { Pad(pad::kA) }));
	r.Apply();
	r.At(0, {});
	r.engine.Reset();
	CHECK(r.At(10, { Pad(pad::kA) }).empty());
	r.At(20, {});
	CHECK(Fired(r.At(30, { Pad(pad::kA) }), Action::Enter));
	r.engine.SetBindings(r.set);
	CHECK(r.At(40, { Pad(pad::kA) }).empty());
}

TEST(press_specificity_largest_chord_wins_when_completed_together)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::Press, { Pad(pad::kA) }));
	r.Bind(Action::Exit, MakeBinding(Trigger::Press, { Pad(pad::kA), Pad(pad::kB) }));
	r.Apply();
	r.At(0, {});
	const auto fired = r.At(10, { Pad(pad::kA), Pad(pad::kB) });  // both in the same snapshot
	CHECK(!Fired(fired, Action::Enter));
	CHECK(Fired(fired, Action::Exit));
}

TEST(disabled_or_empty_bindings_never_fire)
{
	Rig r;
	auto off = MakeBinding(Trigger::Press, { Pad(pad::kA) });
	off.enabled = false;
	r.Bind(Action::Enter, off);
	r.Bind(Action::Exit, MakeBinding(Trigger::Press, {}));
	r.Apply();
	r.At(0, {});
	CHECK(r.At(10, { Pad(pad::kA) }).empty());
	CHECK(r.At(20, {}).empty());
}

TEST(same_chord_on_enter_and_exit_toggles_through_resolve_command)
{
	Rig r;
	r.Bind(Action::Enter, MakeBinding(Trigger::Hold, { Pad(pad::kLB), Pad(pad::kRB) }, 300));
	r.Bind(Action::Exit, MakeBinding(Trigger::Hold, { Pad(pad::kLB), Pad(pad::kRB) }, 300));
	r.Apply();
	r.At(0, {});
	r.At(10, { Pad(pad::kLB), Pad(pad::kRB) });
	const auto fired = r.At(400, { Pad(pad::kLB), Pad(pad::kRB) });
	CHECK(Fired(fired, Action::Enter) && Fired(fired, Action::Exit));
	CHECK(ResolveCommand(fired, false, false) == Command::Enter);
	CHECK(ResolveCommand(fired, true, false) == Command::Exit);
}

TEST(resolve_command_table)
{
	const std::vector<Action> none;
	const std::vector<Action> enter{ Action::Enter };
	const std::vector<Action> exit{ Action::Exit };
	const std::vector<Action> next{ Action::NextPack };
	const std::vector<Action> prev{ Action::PrevPack };
	const std::vector<Action> enterNext{ Action::Enter, Action::NextPack };

	CHECK(ResolveCommand(none, false, true) == Command::None);
	// toggle mode: only the Enter binding matters
	CHECK(ResolveCommand(enter, false, true) == Command::Enter);
	CHECK(ResolveCommand(enter, true, true) == Command::Exit);
	CHECK(ResolveCommand(exit, true, true) == Command::None);
	// separate bindings
	CHECK(ResolveCommand(enter, false, false) == Command::Enter);
	CHECK(ResolveCommand(enter, true, false) == Command::None);
	CHECK(ResolveCommand(exit, true, false) == Command::Exit);
	CHECK(ResolveCommand(exit, false, false) == Command::None);
	// pack cycling
	CHECK(ResolveCommand(next, false, true) == Command::NextPack);
	CHECK(ResolveCommand(prev, true, false) == Command::PrevPack);
	CHECK(ResolveCommand(enterNext, false, true) == Command::Enter);  // enter wins over cycling
}

TEST(cycle_index)
{
	CHECK(CycleIndex(0, -1, 1) == -1);
	CHECK(CycleIndex(3, 0, 1) == 1);
	CHECK(CycleIndex(3, 2, 1) == 0);
	CHECK(CycleIndex(3, 0, -1) == 2);
	CHECK(CycleIndex(3, -1, 1) == 0);
	CHECK(CycleIndex(3, -1, -1) == 2);
	CHECK(CycleIndex(3, 7, 1) == 0);  // out of range current
	CHECK(CycleIndex(1, 0, 1) == 0);
}

// ------------------------------------------------------------------ capture

TEST(capture_ignores_the_input_used_to_start_it_then_records_the_chord)
{
	ChordCapture c;
	c.Begin(0);
	c.Update(0, { Pad(pad::kA) });  // the A that clicked "Assign" is still down
	CHECK(c.GetState() == ChordCapture::State::Waiting);
	c.Update(50, { Pad(pad::kA) });
	CHECK(c.Chord().empty());
	c.Update(100, {});  // released
	CHECK(c.Active());
	c.Update(200, { Pad(pad::kLB) });
	CHECK(c.GetState() == ChordCapture::State::Capturing);
	c.Update(250, { Pad(pad::kLB), Pad(pad::kRB) });
	c.Update(300, { Pad(pad::kRB) });  // LB released first: chord already has both
	CHECK(c.Active());
	c.Update(350, {});
	CHECK(c.GetState() == ChordCapture::State::Done);
	CHECK((c.Chord() == std::vector<InputId>{ Pad(pad::kLB), Pad(pad::kRB) }));
	c.Acknowledge();
	CHECK(c.GetState() == ChordCapture::State::Idle);
	CHECK(c.Chord().empty());
}

TEST(capture_single_quick_tap)
{
	ChordCapture c;
	c.Begin(0);
	c.Update(0, {});
	c.Update(10, { Key('G') });
	c.Update(20, {});
	CHECK(c.GetState() == ChordCapture::State::Done);
	CHECK((c.Chord() == std::vector<InputId>{ Key('G') }));
}

TEST(capture_times_out_when_nothing_is_pressed)
{
	ChordCapture c;
	c.Begin(1000, 8000);
	c.Update(1000, {});
	c.Update(8999, {});
	CHECK(c.GetState() == ChordCapture::State::Waiting);
	c.Update(9000, {});
	CHECK(c.GetState() == ChordCapture::State::TimedOut);
}

TEST(capture_escape_cancels_and_is_never_recorded)
{
	ChordCapture c;
	c.Begin(0);
	c.Update(0, {});
	c.Update(10, { Pad(pad::kA), Key(kVkEscape) });
	CHECK(c.GetState() == ChordCapture::State::Cancelled);
	CHECK(c.Chord().empty());
	c.Update(20, {});  // stays cancelled
	CHECK(c.GetState() == ChordCapture::State::Cancelled);
}

TEST(capture_is_limited_to_four_inputs)
{
	ChordCapture c;
	c.Begin(0);
	c.Update(0, {});
	c.Update(10, { Pad(pad::kA), Pad(pad::kB), Pad(pad::kX), Pad(pad::kY), Pad(pad::kLB), Pad(pad::kRB) });
	c.Update(20, {});
	CHECK(c.GetState() == ChordCapture::State::Done);
	CHECK(c.Chord().size() == kMaxChord);
}

TEST(capture_can_be_cancelled_and_restarted)
{
	ChordCapture c;
	c.Begin(0);
	c.Cancel();
	CHECK(c.GetState() == ChordCapture::State::Cancelled);
	c.Acknowledge();
	c.Begin(100);
	c.Update(100, {});
	c.Update(110, { Pad(pad::kY) });
	c.Update(120, {});
	CHECK(c.GetState() == ChordCapture::State::Done);
	CHECK(c.Chord().size() == 1);
}

// ------------------------------------------------------------------ config

TEST(default_config_has_nothing_bound)
{
	const auto c = MakeDefaultConfig();
	for (const auto& b : c.bindings) {
		CHECK(b.inputs.empty());
	}
	CHECK(c.options.toggleWithEnter);
	CHECK(c.options.language == "it");
}

TEST(config_round_trip_keeps_everything)
{
	auto c = MakeDefaultConfig();
	c.selectedPack = "demo_pack";
	c.options.notify = false;
	c.options.autoExitOnSheathe = true;
	c.options.toggleWithEnter = false;
	c.options.language = "en";
	c.bindings[0] = MakeBinding(Trigger::Hold, { Pad(pad::kLB), Pad(pad::kRB), Pad(pad::kA) }, 750);
	c.bindings[1] = MakeBinding(Trigger::DoubleTap, { Key(0xA2), Key('G') }, 300);
	c.bindings[2] = MakeBinding(Trigger::Tap, { InputId{ Device::Mouse, 5 } }, 200);
	c.bindings[2].enabled = false;

	Config back;
	std::vector<std::string> warnings;
	CHECK(ParseConfig(SerializeConfig(c), back, warnings));
	CHECK(warnings.empty());
	CHECK(back.selectedPack == "demo_pack");
	CHECK(!back.options.notify && back.options.autoExitOnSheathe && !back.options.toggleWithEnter);
	CHECK(back.options.language == "en");
	for (std::size_t i = 0; i < kActionCount; ++i) {
		CHECK(back.bindings[i].enabled == c.bindings[i].enabled);
		CHECK(back.bindings[i].trigger == c.bindings[i].trigger);
		CHECK(back.bindings[i].timeMs == c.bindings[i].timeMs);
		CHECK(back.bindings[i].inputs == c.bindings[i].inputs);
	}
}

TEST(config_parser_is_lenient_with_garbage)
{
	Config c;
	std::vector<std::string> w;
	CHECK(!ParseConfig("not json at all", c, w));
	CHECK(!ParseConfig("[1,2,3]", c, w));
	CHECK(c.selectedPack.empty());

	w.clear();
	const char* text = R"({
		"language": "fr",
		"selectedPack": 42,
		"options": { "notify": "yes", "refreshOnSwap": false },
		"bindings": {
			"enter": { "trigger": "sideways", "timeMs": 999999,
			           "inputs": ["pad:LB", "pad:LB", "pad:nope", 7, "key:Esc", "pad:RB", "pad:A", "pad:B", "pad:X"] },
			"exit": "oops"
		}
	})";
	CHECK(ParseConfig(text, c, w));
	CHECK(c.options.language == "it");        // unsupported language -> default
	CHECK(c.selectedPack.empty());            // wrong type -> default
	CHECK(c.options.notify);                  // wrong type -> default (true)
	CHECK(!c.options.refreshOnSwap);          // valid -> applied
	const auto& e = c.bindings[0];
	CHECK(e.trigger == Trigger::Hold);        // unknown trigger -> default of the Enter binding (Hold)
	CHECK(e.timeMs == kMaxTimeMs);            // clamped
	CHECK((e.inputs == std::vector<InputId>{ Pad(pad::kLB), Pad(pad::kRB), Pad(pad::kA), Pad(pad::kB) }));  // dupes/unknown/Esc dropped, max 4
	CHECK(!w.empty());
}

TEST(config_file_save_and_load)
{
	namespace fs = std::filesystem;
	const auto dir = fs::temp_directory_path() / "swapmoveset_test_cfg" / "nested";
	fs::remove_all(dir.parent_path());
	const auto file = dir / "SwapMoveset.json";

	auto c = MakeDefaultConfig();
	c.selectedPack = "abc";
	c.bindings[0] = MakeBinding(Trigger::Hold, { Pad(pad::kL3), Pad(pad::kR3) }, 500);

	std::string err;
	CHECK(SaveConfigFile(file, c, err));
	CHECK(fs::exists(file));
	CHECK(!fs::exists(fs::path(file).concat(".tmp")));

	Config back;
	std::vector<std::string> w;
	CHECK(LoadConfigFile(file, back, w));
	CHECK(back.selectedPack == "abc");
	CHECK(back.bindings[0].inputs == c.bindings[0].inputs);

	// overwrite
	c.selectedPack = "def";
	CHECK(SaveConfigFile(file, c, err));
	CHECK(LoadConfigFile(file, back, w) && back.selectedPack == "def");

	// missing file -> false + defaults
	CHECK(!LoadConfigFile(dir / "missing.json", back, w));
	CHECK(back.selectedPack.empty());
	fs::remove_all(dir.parent_path());
}

// ------------------------------------------------------------------ packs

TEST(pack_id_rules)
{
	CHECK(IsValidPackId("my_pack"));
	CHECK(IsValidPackId("a-b_c9"));
	CHECK(!IsValidPackId(""));
	CHECK(!IsValidPackId("Has Space"));
	CHECK(!IsValidPackId("UPPER"));
	CHECK(!IsValidPackId("slash/er"));
	CHECK(!IsValidPackId(std::string(49, 'a')));
}

TEST(pack_manifest_parse_and_scan)
{
	namespace fs = std::filesystem;
	PackInfo p;
	std::string err;
	CHECK(ParsePackManifest(R"({"id":"x_1","name":"X","author":"me","order":5,"notes":"n"})", p, err));
	CHECK(p.id == "x_1" && p.name == "X" && p.author == "me" && p.order == 5 && p.notes == "n");
	CHECK(ParsePackManifest(R"({"id":"only_id"})", p, err) && p.name == "only_id");
	CHECK(!ParsePackManifest(R"({"id":"BAD ID"})", p, err));
	CHECK(!ParsePackManifest(R"({"name":"no id"})", p, err));
	CHECK(!ParsePackManifest("garbage", p, err));

	const auto dir = fs::temp_directory_path() / "swapmoveset_test_packs";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const auto write = [&](const char* name, const char* text) { std::ofstream(dir / name) << text; };
	write("b.json", R"({"id":"second","name":"Second","order":20})");
	write("a.json", R"({"id":"first","name":"First","order":10})");
	write("broken.json", "{ nope");
	write("dup.json", R"({"id":"first","name":"Dup"})");
	write("readme.txt", "ignored");
	write("c.json", R"({"id":"third","name":"Alpha","order":20})");

	std::vector<std::string> w;
	const auto packs = ScanPacks(dir, w);
	CHECK(packs.size() == 3);
	if (packs.size() == 3) {
		CHECK(packs[0].id == "first");
		CHECK(packs[1].id == "third");   // same order as "second", sorted by name (Alpha < Second)
		CHECK(packs[2].id == "second");
	}
	CHECK(w.size() == 2);  // broken + duplicate
	CHECK(ScanPacks(dir / "missing", w).empty());
	fs::remove_all(dir);
}

// ------------------------------------------------------------------ main

int main()
{
	for (const auto& t : Registry()) {
		g_current = t.name;
		const int before = g_failures;
		t.fn();
		std::printf("%s %s\n", g_failures == before ? "[ ok ]" : "[FAIL]", t.name);
	}
	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
