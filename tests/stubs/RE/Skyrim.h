#pragma once
// Minimal stand-in for CommonLibSSE-NG, just enough to compile and *run* the plugin's own code on Linux
// (tests/test_runtime.cpp). It mirrors only the signatures the plugin uses; the real ones were checked
// against the CommonLibSSE-NG sources. Nothing here ships in the plugin.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace REX::W32
{
	using HWND = struct HWND__*;
	using HINSTANCE = struct HINSTANCE__*;
	using HMODULE = HINSTANCE;

	struct XINPUT_GAMEPAD
	{
		std::uint16_t buttons;
		std::uint8_t  leftTrigger;
		std::uint8_t  rightTrigger;
		std::int16_t  thumbLX;
		std::int16_t  thumbLY;
		std::int16_t  thumbRX;
		std::int16_t  thumbRY;
	};
	struct XINPUT_STATE
	{
		std::uint32_t  packetNumber;
		XINPUT_GAMEPAD gamepad;
	};

	// defined by the test
	HMODULE LoadLibraryA(const char* a_name) noexcept;
	HMODULE GetModuleHandleA(const char* a_name) noexcept;
	void*   GetProcAddress(HMODULE a_module, const char* a_name) noexcept;
}

namespace fake
{
	struct Game
	{
		bool is3DLoaded{ true };
		bool dead{ false };
		bool ragdoll{ false };
		bool mounted{ false };
		bool killMove{ false };
		bool staggered{ false };
		bool swimming{ false };
		bool weaponDrawn{ false };
		bool freezeTime{ false };
		bool paused{ false };
		bool modal{ false };
		int  attack{ 0 };  // 0 = none
		std::vector<std::string> graphEvents;
		std::vector<std::string> hud;
		void*                    window{ reinterpret_cast<void*>(0x1234) };
	};
	inline Game game;
}

namespace RE
{
	class BSFixedString
	{
	public:
		BSFixedString(const char* a_str) :
			str(a_str) {}
		std::string str;
	};

	enum class ATTACK_STATE_ENUM : std::uint32_t
	{
		kNone = 0,
		kSwing = 2
	};
	enum class KNOCK_STATE_ENUM : std::uint32_t
	{
		kNormal = 0,
		kDown = 7
	};
	enum class SIT_SLEEP_STATE : std::uint32_t
	{
		kNormal = 0,
		kIsSitting = 3
	};
	enum class FLY_STATE : std::uint32_t
	{
		kNone = 0,
		kCruising = 2
	};

	class ActorState
	{
	public:
		ATTACK_STATE_ENUM GetAttackState() const noexcept { return static_cast<ATTACK_STATE_ENUM>(fake::game.attack); }
		KNOCK_STATE_ENUM  GetKnockState() const noexcept { return KNOCK_STATE_ENUM::kNormal; }
		SIT_SLEEP_STATE   GetSitSleepState() const noexcept { return SIT_SLEEP_STATE::kNormal; }
		FLY_STATE         GetFlyState() const noexcept { return FLY_STATE::kNone; }
		bool              IsStaggered() const noexcept { return fake::game.staggered; }
		bool              IsSwimming() const noexcept { return fake::game.swimming; }
		bool              IsWeaponDrawn() const noexcept { return fake::game.weaponDrawn; }
	};

	class PlayerCharacter
	{
	public:
		static PlayerCharacter* GetSingleton()
		{
			static PlayerCharacter p;
			return &p;
		}
		bool        Is3DLoaded() const { return fake::game.is3DLoaded; }
		bool        IsDead(bool = true) const { return fake::game.dead; }
		bool        IsInRagdollState() const { return fake::game.ragdoll; }
		bool        IsOnMount() const { return fake::game.mounted; }
		bool        IsInKillMove() const noexcept { return fake::game.killMove; }
		ActorState* AsActorState() noexcept { return &_state; }
		const ActorState* AsActorState() const noexcept { return &_state; }
		bool              NotifyAnimationGraph(const BSFixedString& a_event)
		{
			fake::game.graphEvents.push_back(a_event.str);
			return true;
		}

	private:
		ActorState _state;
	};

	class Main
	{
	public:
		static Main* GetSingleton()
		{
			static Main m;
			return &m;
		}
		struct RUNTIME_DATA
		{
			bool freezeTime{ false };
		};
		RUNTIME_DATA&       GetRuntimeData() noexcept { return _runtime; }
		const RUNTIME_DATA& GetRuntimeData() const noexcept { return _runtime; }
		REX::W32::HWND      wnd{ nullptr };
		Main() { wnd = reinterpret_cast<REX::W32::HWND>(fake::game.window); }

	private:
		RUNTIME_DATA _runtime;
	};

	class UI
	{
	public:
		static UI* GetSingleton()
		{
			static UI u;
			return &u;
		}
		bool GameIsPaused() { return fake::game.paused; }
		bool IsModalMenuOpen() const { return fake::game.modal; }
		bool IsMenuOpen(const std::string_view&) { return false; }
	};

	struct DialogueMenu
	{
		constexpr static std::string_view MENU_NAME = "Dialogue Menu";
	};
	struct LoadingMenu
	{
		constexpr static std::string_view MENU_NAME = "Loading Menu";
	};

	namespace SendHUDMessage
	{
		inline void ShowHUDMessage(const char* a_text, const char* = nullptr, bool = true)
		{
			fake::game.hud.emplace_back(a_text);
		}
	}

	// --- only for compiling the OAR API headers (PackActiveCondition.cpp) --------------------------------
	class BSString
	{
	public:
		BSString(const char* a_str = "") :
			_s(a_str) {}
		BSString(std::string_view a_str) :
			_s(a_str) {}
		const char*  data() const { return _s.c_str(); }
		std::size_t  length() const { return _s.size(); }

	private:
		std::string _s;
	};
	class hkbClipGenerator
	{};
	class TESForm
	{};
	class TESGlobal
	{};
	class BGSKeyword
	{};
	class BGSKeywordForm
	{};
	struct NiPoint3
	{
		float x, y, z;
	};
	enum class ActorValue : std::int32_t
	{
		kNone = -1
	};
	namespace BSVisit
	{
		enum class BSVisitControl
		{
			kContinue = 0,
			kStop = 1
		};
	}
	class TESObjectREFR
	{
	public:
		bool IsPlayerRef() const { return true; }
	};
}
