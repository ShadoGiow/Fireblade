#include "Binding.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace sm
{
	std::string_view ActionToken(Action a_action) noexcept
	{
		switch (a_action) {
		case Action::Enter: return "enter";
		case Action::Exit: return "exit";
		case Action::NextPack: return "nextPack";
		case Action::PrevPack: return "prevPack";
		default: return "?";
		}
	}

	std::string_view TriggerToken(Trigger a_trigger) noexcept
	{
		switch (a_trigger) {
		case Trigger::Press: return "press";
		case Trigger::Tap: return "tap";
		case Trigger::Hold: return "hold";
		case Trigger::DoubleTap: return "doubletap";
		}
		return "press";
	}

	bool TriggerFromToken(std::string_view a_token, Trigger& a_out)
	{
		std::string t(a_token);
		std::ranges::transform(t, t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (t == "press") {
			a_out = Trigger::Press;
		} else if (t == "tap") {
			a_out = Trigger::Tap;
		} else if (t == "hold") {
			a_out = Trigger::Hold;
		} else if (t == "doubletap" || t == "double") {
			a_out = Trigger::DoubleTap;
		} else {
			return false;
		}
		return true;
	}

	std::uint32_t Binding::EffectiveMs() const noexcept
	{
		std::uint32_t ms = timeMs;
		if (ms == 0) {
			switch (trigger) {
			case Trigger::Hold: ms = kDefaultHoldMs; break;
			case Trigger::Tap: ms = kDefaultTapMs; break;
			case Trigger::DoubleTap: ms = kDefaultDoubleTapMs; break;
			case Trigger::Press: return 0;
			}
		}
		return std::clamp(ms, kMinTimeMs, kMaxTimeMs);
	}
}
