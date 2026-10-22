#pragma once
// Portable (no engine dependencies): this header is unit-tested on Linux.

#include "Input.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sm
{
	/// What a binding can ask for. Which of them are used is decided by ResolveCommand().
	enum class Action : std::uint8_t
	{
		Enter = 0,
		Exit,
		NextPack,
		PrevPack,
		Count
	};
	inline constexpr std::size_t kActionCount = static_cast<std::size_t>(Action::Count);

	[[nodiscard]] std::string_view ActionToken(Action a_action) noexcept;  // "enter", "exit", ...

	enum class Trigger : std::uint8_t
	{
		Press,      // fires the moment the whole chord is down
		Tap,        // fires when the chord is released, if it was held for at most timeMs
		Hold,       // fires once the chord has been held for timeMs
		DoubleTap   // fires on the second press, if the first was short and the gap is within timeMs
	};

	[[nodiscard]] std::string_view TriggerToken(Trigger a_trigger) noexcept;  // "press", "tap", "hold", "doubletap"
	[[nodiscard]] bool             TriggerFromToken(std::string_view a_token, Trigger& a_out);

	inline constexpr std::size_t kMaxChord = 4;

	struct Binding
	{
		bool                 enabled{ true };
		Trigger              trigger{ Trigger::Press };
		std::uint32_t        timeMs{ 0 };  // 0 = default for the trigger
		std::vector<InputId> inputs;       // the chord: every input must be down together

		[[nodiscard]] std::uint32_t EffectiveMs() const noexcept;
		[[nodiscard]] bool          IsValid() const noexcept { return !inputs.empty() && inputs.size() <= kMaxChord; }

		friend bool operator==(const Binding&, const Binding&) = default;
	};

	using BindingSet = std::array<Binding, kActionCount>;

	inline constexpr std::uint32_t kDefaultHoldMs = 600;
	inline constexpr std::uint32_t kDefaultTapMs = 250;
	inline constexpr std::uint32_t kDefaultDoubleTapMs = 350;
	inline constexpr std::uint32_t kMinTimeMs = 100;
	inline constexpr std::uint32_t kMaxTimeMs = 3000;
}
