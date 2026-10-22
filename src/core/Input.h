#pragma once
// Portable (no engine dependencies): this header is unit-tested on Linux.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sm
{
	enum class Device : std::uint8_t
	{
		Keyboard = 0,  // code = Windows virtual-key code
		Mouse = 1,     // code = Windows virtual-key code (Middle / X1 / X2 only)
		Gamepad = 2    // code = XInput button mask, plus the two synthetic trigger codes below
	};

	struct InputId
	{
		Device        device{ Device::Keyboard };
		std::uint32_t code{ 0 };

		friend constexpr bool operator==(const InputId&, const InputId&) = default;
		friend constexpr auto operator<=>(const InputId&, const InputId&) = default;
	};

	namespace pad
	{
		inline constexpr std::uint32_t kUp = 0x0001;
		inline constexpr std::uint32_t kDown = 0x0002;
		inline constexpr std::uint32_t kLeft = 0x0004;
		inline constexpr std::uint32_t kRight = 0x0008;
		inline constexpr std::uint32_t kStart = 0x0010;
		inline constexpr std::uint32_t kBack = 0x0020;
		inline constexpr std::uint32_t kL3 = 0x0040;
		inline constexpr std::uint32_t kR3 = 0x0080;
		inline constexpr std::uint32_t kLB = 0x0100;
		inline constexpr std::uint32_t kRB = 0x0200;
		inline constexpr std::uint32_t kA = 0x1000;
		inline constexpr std::uint32_t kB = 0x2000;
		inline constexpr std::uint32_t kX = 0x4000;
		inline constexpr std::uint32_t kY = 0x8000;
		// Not part of the XInput button mask: analogue triggers, thresholded by the poller.
		inline constexpr std::uint32_t kLT = 0x10000;
		inline constexpr std::uint32_t kRT = 0x20000;
	}

	inline constexpr std::uint32_t kVkEscape = 0x1B;

	/// Every input the poller looks at (and that can appear in a binding).
	[[nodiscard]] const std::vector<InputId>& PollableInputs();

	/// Config token such as "pad:LB", "key:LCtrl", "key:G", "mouse:Middle". Unknown codes fall back
	/// to "pad:#4096"-style tokens so nothing is ever lost on a save/load round trip.
	[[nodiscard]] std::string ToToken(InputId a_id);
	[[nodiscard]] bool        FromToken(std::string_view a_token, InputId& a_out);

	/// Short human-readable name for the UI ("LB", "Left Ctrl", "G").
	[[nodiscard]] std::string DisplayName(InputId a_id);
	/// "LB + RB + A"
	[[nodiscard]] std::string DescribeChord(const std::vector<InputId>& a_chord);

	/// Inputs that can never be bound (Esc is reserved to cancel the capture).
	[[nodiscard]] constexpr bool IsUnbindable(InputId a_id) noexcept
	{
		return a_id.device == Device::Keyboard && a_id.code == kVkEscape;
	}
}
