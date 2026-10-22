#pragma once
// Portable (no engine dependencies): this header is unit-tested on Linux.
//
// The engine is fed with *snapshots*: "these inputs are down right now, at time T". It derives the
// edges itself, so it does not depend on the game's input events (which SKSE Menu Framework swallows
// while its menu is open) and it behaves identically for keyboard, mouse and gamepad.

#include "Binding.h"

#include <array>
#include <cstdint>
#include <vector>

namespace sm
{
	class BindingEngine
	{
	public:
		using Bindings = BindingSet;

		/// Replaces the bindings and forgets all runtime state. Inputs that are already down at the next
		/// Update() are ignored until they have been released ("quarantine"), so changing the config
		/// while a button is held can never fire anything.
		void SetBindings(const Bindings& a_bindings);

		/// Same quarantine behaviour, keeps the bindings. Call after anything that makes the held
		/// state unreliable (focus loss, load screens...).
		void Reset();

		/// a_down = every input currently down. Appends the actions that fired at this instant (each
		/// action at most once, in enum order).
		void Update(std::uint64_t a_nowMs, const std::vector<InputId>& a_down, std::vector<Action>& a_fired);

	private:
		struct Runtime
		{
			bool          chordHeld{ false };
			std::uint64_t chordSince{ 0 };
			bool          holdFired{ false };
			bool          firedOnThisPress{ false };
			bool          tapValid{ false };  // DoubleTap: a short first tap has been seen
			std::uint64_t tapReleasedAt{ 0 };
		};

		Bindings                         _bindings{};
		std::array<Runtime, kActionCount> _runtime{};
		std::vector<InputId>             _quarantine;
		bool                             _snapshotQuarantine{ true };
	};

	/// Records a new chord from live input: every input pressed from now on is added (up to kMaxChord)
	/// and the capture completes when everything has been released again.
	class ChordCapture
	{
	public:
		enum class State : std::uint8_t
		{
			Idle,
			Waiting,    // nothing pressed yet
			Capturing,  // at least one input is down
			Done,       // chord() holds the result
			Cancelled,  // Esc
			TimedOut
		};

		/// Inputs that are already down when the capture starts (e.g. the button used to click the
		/// "Assign" widget) are ignored until they are released.
		void Begin(std::uint64_t a_nowMs, std::uint32_t a_timeoutMs = 8000);
		void Cancel() noexcept;
		void Update(std::uint64_t a_nowMs, const std::vector<InputId>& a_down);
		/// Back to Idle (call once the Done/Cancelled/TimedOut result has been consumed).
		void Acknowledge() noexcept;

		[[nodiscard]] State                       GetState() const noexcept { return _state; }
		[[nodiscard]] bool                        Active() const noexcept { return _state == State::Waiting || _state == State::Capturing; }
		[[nodiscard]] const std::vector<InputId>& Chord() const noexcept { return _chord; }
		[[nodiscard]] const std::vector<InputId>& Pressed() const noexcept { return _pressed; }

	private:
		State                _state{ State::Idle };
		std::vector<InputId> _chord;
		std::vector<InputId> _pressed;
		std::vector<InputId> _quarantine;
		bool                 _snapshotQuarantine{ true };
		std::uint64_t        _beganAt{ 0 };
		std::uint32_t        _timeoutMs{ 8000 };
	};

	/// What the mode controller should do after a batch of actions fired together.
	enum class Command : std::uint8_t
	{
		None,
		Enter,
		Exit,
		NextPack,
		PrevPack
	};

	/// - toggleWithEnter: the Enter binding toggles (Exit binding is ignored).
	/// - otherwise Exit only acts while active, Enter only while inactive. If both fire together (same
	///   chord assigned to both) they behave as a toggle.
	/// - Pack cycling is lowest priority.
	[[nodiscard]] Command ResolveCommand(const std::vector<Action>& a_fired, bool a_active, bool a_toggleWithEnter) noexcept;

	/// Next / previous index in a list of a_count items, wrapping around. a_current may be -1.
	[[nodiscard]] int CycleIndex(int a_count, int a_current, int a_direction) noexcept;
}
