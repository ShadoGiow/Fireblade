#pragma once

#include "core/Input.h"

#include <cstdint>
#include <vector>

namespace sm
{
	/// Reads the keyboard, mouse buttons and XInput gamepad directly from Windows.
	///
	/// Why not the game's input events? SKSE Menu Framework replaces the game's input dispatch with an
	/// empty event while its F1 menu is open, so a normal event sink would be blind exactly when the
	/// user is assigning keys. Polling also gives one uniform source (and the same timing) for
	/// gameplay and for the "press the keys you want" capture.
	///
	/// All Win32 entry points are resolved at runtime through REX::W32, so no <Windows.h> is needed.
	class InputPoller
	{
	public:
		/// a_gameWindow: the game's HWND (RE::Main::wnd), used to ignore input while the game is in the
		/// background. May be null (then the game is always considered focused).
		void Init(void* a_gameWindow);

		[[nodiscard]] bool GameHasFocus() const;

		/// Replaces a_out with every input that is currently down.
		void Poll(std::vector<InputId>& a_out);

	private:
		using GetAsyncKeyStateFn = std::int16_t (*)(int);
		using GetForegroundWindowFn = void* (*)();
		using XInputGetStateFn = std::uint32_t (*)(std::uint32_t, void*);  // really XINPUT_STATE*

		void*                 _gameWindow{ nullptr };
		GetAsyncKeyStateFn    _getAsyncKeyState{ nullptr };
		GetForegroundWindowFn _getForegroundWindow{ nullptr };
		XInputGetStateFn      _xinputGetState{ nullptr };

		int           _padUser{ -1 };       // XInput user index of the connected pad, -1 = none
		std::uint64_t _nextPadScanMs{ 0 };  // disconnected pads are expensive to query: rescan rarely
	};
}
