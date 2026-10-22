#include "Input.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>

namespace sm
{
	namespace
	{
		struct Entry
		{
			Device           device;
			std::uint32_t    code;
			std::string_view token;
			std::string_view display;
		};

		constexpr Device K = Device::Keyboard;
		constexpr Device M = Device::Mouse;
		constexpr Device G = Device::Gamepad;

		// Windows virtual-key codes for the keyboard / mouse entries.
		const std::vector<Entry>& Table()
		{
			static const std::vector<Entry> table = [] {
				std::vector<Entry> t;
				// --- gamepad ---
				t.push_back({ G, pad::kUp, "DPadUp", "D-Pad Up" });
				t.push_back({ G, pad::kDown, "DPadDown", "D-Pad Down" });
				t.push_back({ G, pad::kLeft, "DPadLeft", "D-Pad Left" });
				t.push_back({ G, pad::kRight, "DPadRight", "D-Pad Right" });
				t.push_back({ G, pad::kStart, "Start", "Start" });
				t.push_back({ G, pad::kBack, "Back", "Back" });
				t.push_back({ G, pad::kL3, "L3", "L3" });
				t.push_back({ G, pad::kR3, "R3", "R3" });
				t.push_back({ G, pad::kLB, "LB", "LB" });
				t.push_back({ G, pad::kRB, "RB", "RB" });
				t.push_back({ G, pad::kLT, "LT", "LT" });
				t.push_back({ G, pad::kRT, "RT", "RT" });
				t.push_back({ G, pad::kA, "A", "A" });
				t.push_back({ G, pad::kB, "B", "B" });
				t.push_back({ G, pad::kX, "X", "X" });
				t.push_back({ G, pad::kY, "Y", "Y" });
				// --- mouse (left / right are deliberately not bindable) ---
				t.push_back({ M, 0x04, "Middle", "Mouse Middle" });
				t.push_back({ M, 0x05, "X1", "Mouse X1" });
				t.push_back({ M, 0x06, "X2", "Mouse X2" });
				// --- keyboard ---
				static const char* const kLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
				static const char* const kDigits = "0123456789";
				static const std::string_view kLetterTokens[26] = {
					"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
					"N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"
				};
				static const std::string_view kDigitTokens[10] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
				for (int i = 0; i < 26; ++i) {
					t.push_back({ K, static_cast<std::uint32_t>(kLetters[i]), kLetterTokens[i], kLetterTokens[i] });
				}
				for (int i = 0; i < 10; ++i) {
					t.push_back({ K, static_cast<std::uint32_t>(kDigits[i]), kDigitTokens[i], kDigitTokens[i] });
				}
				static const std::string_view kFn[12] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
				for (int i = 0; i < 12; ++i) {
					t.push_back({ K, static_cast<std::uint32_t>(0x70 + i), kFn[i], kFn[i] });
				}
				static const std::string_view kNum[10] = { "Num0", "Num1", "Num2", "Num3", "Num4", "Num5", "Num6", "Num7", "Num8", "Num9" };
				for (int i = 0; i < 10; ++i) {
					t.push_back({ K, static_cast<std::uint32_t>(0x60 + i), kNum[i], kNum[i] });
				}
				t.push_back({ K, 0x1B, "Esc", "Esc" });
				t.push_back({ K, 0x09, "Tab", "Tab" });
				t.push_back({ K, 0x14, "CapsLock", "Caps Lock" });
				t.push_back({ K, 0xA0, "LShift", "Left Shift" });
				t.push_back({ K, 0xA1, "RShift", "Right Shift" });
				t.push_back({ K, 0xA2, "LCtrl", "Left Ctrl" });
				t.push_back({ K, 0xA3, "RCtrl", "Right Ctrl" });
				t.push_back({ K, 0xA4, "LAlt", "Left Alt" });
				t.push_back({ K, 0xA5, "RAlt", "Right Alt" });
				t.push_back({ K, 0x20, "Space", "Space" });
				t.push_back({ K, 0x0D, "Enter", "Enter" });
				t.push_back({ K, 0x08, "Backspace", "Backspace" });
				t.push_back({ K, 0x2D, "Insert", "Insert" });
				t.push_back({ K, 0x2E, "Delete", "Delete" });
				t.push_back({ K, 0x24, "Home", "Home" });
				t.push_back({ K, 0x23, "End", "End" });
				t.push_back({ K, 0x21, "PageUp", "Page Up" });
				t.push_back({ K, 0x22, "PageDown", "Page Down" });
				t.push_back({ K, 0x25, "ArrowLeft", "Arrow Left" });
				t.push_back({ K, 0x26, "ArrowUp", "Arrow Up" });
				t.push_back({ K, 0x27, "ArrowRight", "Arrow Right" });
				t.push_back({ K, 0x28, "ArrowDown", "Arrow Down" });
				t.push_back({ K, 0x6A, "NumMultiply", "Num *" });
				t.push_back({ K, 0x6B, "NumAdd", "Num +" });
				t.push_back({ K, 0x6D, "NumSubtract", "Num -" });
				t.push_back({ K, 0x6E, "NumDecimal", "Num ." });
				t.push_back({ K, 0x6F, "NumDivide", "Num /" });
				t.push_back({ K, 0xBA, "Semicolon", ";" });
				t.push_back({ K, 0xBB, "Equals", "=" });
				t.push_back({ K, 0xBC, "Comma", "," });
				t.push_back({ K, 0xBD, "Minus", "-" });
				t.push_back({ K, 0xBE, "Period", "." });
				t.push_back({ K, 0xBF, "Slash", "/" });
				t.push_back({ K, 0xC0, "Grave", "`" });
				t.push_back({ K, 0xDB, "LBracket", "[" });
				t.push_back({ K, 0xDC, "Backslash", "\\" });
				t.push_back({ K, 0xDD, "RBracket", "]" });
				t.push_back({ K, 0xDE, "Quote", "'" });
				t.push_back({ K, 0x2C, "PrintScreen", "Print Screen" });
				t.push_back({ K, 0x91, "ScrollLock", "Scroll Lock" });
				t.push_back({ K, 0x13, "Pause", "Pause" });
				t.push_back({ K, 0x90, "NumLock", "Num Lock" });
				return t;
			}();
			return table;
		}

		const char* Prefix(Device a_device) noexcept
		{
			switch (a_device) {
			case Device::Keyboard: return "key";
			case Device::Mouse: return "mouse";
			case Device::Gamepad: return "pad";
			}
			return "key";
		}

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		const Entry* Find(InputId a_id)
		{
			for (const auto& e : Table()) {
				if (e.device == a_id.device && e.code == a_id.code) {
					return &e;
				}
			}
			return nullptr;
		}
	}

	const std::vector<InputId>& PollableInputs()
	{
		static const std::vector<InputId> inputs = [] {
			std::vector<InputId> v;
			v.reserve(Table().size());
			for (const auto& e : Table()) {
				v.push_back({ e.device, e.code });
			}
			return v;
		}();
		return inputs;
	}

	std::string ToToken(InputId a_id)
	{
		if (const auto* e = Find(a_id)) {
			return std::string(Prefix(a_id.device)) + ":" + std::string(e->token);
		}
		return std::string(Prefix(a_id.device)) + ":#" + std::to_string(a_id.code);
	}

	bool FromToken(std::string_view a_token, InputId& a_out)
	{
		const auto colon = a_token.find(':');
		if (colon == std::string_view::npos || colon == 0 || colon + 1 >= a_token.size()) {
			return false;
		}
		const auto prefix = Lower(a_token.substr(0, colon));
		const auto name = a_token.substr(colon + 1);

		Device device;
		if (prefix == "key") {
			device = Device::Keyboard;
		} else if (prefix == "mouse") {
			device = Device::Mouse;
		} else if (prefix == "pad") {
			device = Device::Gamepad;
		} else {
			return false;
		}

		if (name.front() == '#') {
			std::uint32_t code = 0;
			const auto    digits = name.substr(1);
			const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), code);
			if (ec != std::errc{} || ptr != digits.data() + digits.size() || code == 0) {
				return false;
			}
			a_out = { device, code };
			return true;
		}

		const auto wanted = Lower(name);
		for (const auto& e : Table()) {
			if (e.device == device && Lower(e.token) == wanted) {
				a_out = { e.device, e.code };
				return true;
			}
		}
		return false;
	}

	std::string DisplayName(InputId a_id)
	{
		if (const auto* e = Find(a_id)) {
			return std::string(e->display);
		}
		return std::string(Prefix(a_id.device)) + " #" + std::to_string(a_id.code);
	}

	std::string DescribeChord(const std::vector<InputId>& a_chord)
	{
		std::string out;
		for (std::size_t i = 0; i < a_chord.size(); ++i) {
			if (i) {
				out += " + ";
			}
			out += DisplayName(a_chord[i]);
		}
		return out;
	}
}
