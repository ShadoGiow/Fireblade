#pragma once

#include "PCH.h"

// Shared declarations. Personal-use mashup helper: replaces the dual-wield, weapon-art and key-trace
// dependencies of a weapon-combo mod with native code and an in-game menu.

namespace ESA
{
	// ------------------------------------------------------------------ gamepad bit masks
	// XInput wButtons masks; the triggers have no mask in XInput so they get private bits.
	namespace Btn
	{
		inline constexpr std::uint32_t DUp = 0x0001, DDown = 0x0002, DLeft = 0x0004, DRight = 0x0008;
		inline constexpr std::uint32_t Start = 0x0010, Back = 0x0020, L3 = 0x0040, R3 = 0x0080;
		inline constexpr std::uint32_t LB = 0x0100, RB = 0x0200;
		inline constexpr std::uint32_t A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000;
		inline constexpr std::uint32_t LT = 0x10000, RT = 0x20000;
	}

	// ------------------------------------------------------------------ settings
	enum class StanceMode : int
	{
		FireSpell = 0,   // left hand: spell with a fire effect
		Destruction = 1, // left hand: any destruction spell
		AnySpell = 2,    // left hand: any spell
		AnyHand = 3      // blade in the right hand is enough
	};

	struct Settings
	{
		// general
		std::string pluginFile;  // data plugin that holds the forms (set in the INI, no default on purpose)
		bool enabled = true;
		bool verbose = false;

		// stance
		int stanceMode = static_cast<int>(StanceMode::FireSpell);

		// inputs
		bool          padEnabled = true;
		std::uint32_t ultChord = Btn::L3;
		bool          ultNeutralStick = true;
		float         deadzone = 0.40f;
		bool          padForwardComb = true;
		bool          padBackExec = true;
		bool          kbFallback = true;  // A+D / W / S as in the original

		// resource markers: 0 = always available
		int magSkill = 0;
		int magUltSkill = 0;
		int magUlt = 0;

		// chase (follow-up dash after the mark chain)
		bool  chaseAuto = true;
		float chaseDelay = 0.75f;

		// incineration (ground fire cast in front of the player)
		bool  incEnabled = true;
		int   incExtra = 2;
		float incDist = 100.0f;
		float incStep = 30.0f;
		float incDelay = 0.10f;

		// fire burst (explosion placed on the target)
		bool burstEnabled = true;

		// Re-Ignition (low-health revive-style heal, previously tied to the dual-wield dummy weapon)
		bool reignEnabled = true;
		int  reignPercent = 20;
		bool reignNeedCombat = true;

		// cosmetics
		bool ghostBlade = true;
	};

	extern Settings S;

	namespace Ini
	{
		void Load();
		void Save();
		void Reset();
	}

	// ------------------------------------------------------------------ forms
	enum Marker : int
	{
		kStance = 0,
		kUlt,
		kComb,
		kExec,
		kMagSkill,
		kMagUltSkill,
		kMagUlt,
		kMarkerCount
	};

	inline constexpr const char* kMarkerNames[kMarkerCount] = {
		"Stance", "Ult chord", "Forward (comb)", "Back (exec)", "Res: skill", "Res: ult skill", "Res: ult"
	};

	struct MarkerForm
	{
		RE::EffectSetting* mgef = nullptr;
		RE::SpellItem*     spell = nullptr;
	};

	struct Forms
	{
		bool ok = false;

		MarkerForm marker[kMarkerCount];

		RE::BGSKeyword* kwBlade = nullptr;      // 84A  weapon keyword

		RE::SpellItem*     spReign = nullptr;   // E15
		RE::EffectSetting* mgReignCd = nullptr; // E16

		RE::EffectSetting* mgChase = nullptr;   // E2A
		RE::EffectSetting* mgChain = nullptr;   // E28
		RE::SpellItem*     spChain = nullptr;   // E29

		RE::EffectSetting* mgIncin = nullptr;   // E32
		RE::SpellItem*     spIncin = nullptr;   // E34

		RE::EffectSetting* mgBurst = nullptr;   // E5A
		RE::BGSExplosion*  explBurst = nullptr; // E5B

		RE::EffectSetting* mgGhost = nullptr;   // 866
		RE::SpellItem*     spGhost = nullptr;   // 864

		RE::TESBoundObject* xmarker = nullptr;  // vanilla XMarker
	};

	extern Forms F;

	bool ResolveForms();

	// ------------------------------------------------------------------ logging
	void                     LogLine(const std::string& a_line);
	std::vector<std::string> LogSnapshot();

	template <class... Args>
	void Logf(std::format_string<Args...> a_fmt, Args&&... a_args)
	{
		LogLine(std::format(a_fmt, std::forward<Args>(a_args)...));
	}

	template <class... Args>
	void Vlogf(std::format_string<Args...> a_fmt, Args&&... a_args)
	{
		if (S.verbose) {
			LogLine(std::format(a_fmt, std::forward<Args>(a_args)...));
		}
	}

	// ------------------------------------------------------------------ runtime state shown in the menu
	struct Status
	{
		bool        stance = false;
		bool        bladeEquipped = false;
		std::string rightName;
		std::string leftName;
		bool        leftIsSpell = false;
		bool        leftIsFire = false;
		bool        desired[kMarkerCount]{};
		bool        applied[kMarkerCount]{};
		std::uint32_t padHeld = 0;
		float       lsx = 0.0f;
		float       lsy = 0.0f;
		bool        ultHeld = false;
		bool        fwdHeld = false;
		bool        backHeld = false;
		float       magicka = 0.0f;
		float       healthPct = 0.0f;
		bool        reignCooldown = false;
	};

	Status StatusSnapshot();

	// ------------------------------------------------------------------ input state (written by the input sink)
	struct InputState
	{
		std::atomic<std::uint32_t> padHeld{ 0 };
		std::atomic<float>         lsx{ 0.0f };
		std::atomic<float>         lsy{ 0.0f };
		std::atomic<bool>          kbW{ false }, kbA{ false }, kbS{ false }, kbD{ false };
	};

	extern InputState In;

	// ------------------------------------------------------------------ core API
	void StartCore();                        // starts the polling thread (once)
	void OnGameLoaded();                     // new game / save loaded: resync markers and timers
	void OnGameLoading();                    // before a save is loaded
	void Tick();                             // main-thread update (also callable from tasks)
	void MarkSettingsDirty();

	// effect handlers (called from the magic effect sink)
	void OnMagicEffectApplied(RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_caster, RE::FormID a_effect);

	// debug actions triggered from the menu
	enum class Action : int
	{
		None = 0,
		RemoveMarkers,
		Resync,
		TestReignition,
		TestChase,
		TestIncinerate,
		TestBurst
	};
	void RequestAction(Action a_action);

	// events
	void RegisterSinks();

	// menu
	namespace Menu
	{
		void Register();
	}
}
