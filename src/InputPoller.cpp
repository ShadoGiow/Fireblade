#include "InputPoller.h"

#include <algorithm>
#include <chrono>

namespace sm
{
	namespace
	{
		constexpr std::uint8_t kTriggerThreshold = 100;  // 0..255, a firm squeeze rather than a graze

		std::uint64_t NowMs()
		{
			using namespace std::chrono;
			return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
		}

		template <class Fn>
		Fn Resolve(REX::W32::HMODULE a_module, const char* a_name)
		{
			return a_module ? reinterpret_cast<Fn>(REX::W32::GetProcAddress(a_module, a_name)) : nullptr;
		}
	}

	void InputPoller::Init(void* a_gameWindow)
	{
		_gameWindow = a_gameWindow;

		if (const auto user32 = REX::W32::LoadLibraryA("user32.dll")) {
			_getAsyncKeyState = Resolve<GetAsyncKeyStateFn>(user32, "GetAsyncKeyState");
			_getForegroundWindow = Resolve<GetForegroundWindowFn>(user32, "GetForegroundWindow");
		}
		if (!_getAsyncKeyState) {
			logs::error("InputPoller: GetAsyncKeyState not available - keyboard/mouse bindings disabled");
		}

		for (const char* dll : { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" }) {
			if (const auto mod = REX::W32::LoadLibraryA(dll)) {
				_xinputGetState = Resolve<XInputGetStateFn>(mod, "XInputGetState");
				if (_xinputGetState) {
					logs::info("InputPoller: using {}", dll);
					break;
				}
			}
		}
		if (!_xinputGetState) {
			logs::warn("InputPoller: XInput not available - gamepad bindings disabled");
		}
	}

	bool InputPoller::GameHasFocus() const
	{
		if (!_gameWindow || !_getForegroundWindow) {
			return true;
		}
		return _getForegroundWindow() == _gameWindow;
	}

	void InputPoller::Poll(std::vector<InputId>& a_out)
	{
		a_out.clear();

		if (_getAsyncKeyState) {
			for (const auto& id : PollableInputs()) {
				if (id.device == Device::Gamepad) {
					continue;
				}
				if ((_getAsyncKeyState(static_cast<int>(id.code)) & 0x8000) != 0) {
					a_out.push_back(id);
				}
			}
		}

		if (!_xinputGetState) {
			return;
		}

		REX::W32::XINPUT_STATE state{};
		bool                   have = false;

		if (_padUser >= 0) {
			if (_xinputGetState(static_cast<std::uint32_t>(_padUser), &state) == 0) {
				have = true;
			} else {
				_padUser = -1;  // unplugged
				_nextPadScanMs = 0;
			}
		}
		if (!have) {
			const auto now = NowMs();
			if (now >= _nextPadScanMs) {
				_nextPadScanMs = now + 1000;
				for (std::uint32_t user = 0; user < 4; ++user) {
					if (_xinputGetState(user, &state) == 0) {
						_padUser = static_cast<int>(user);
						have = true;
						break;
					}
				}
			}
		}
		if (!have) {
			return;
		}

		const auto buttons = static_cast<std::uint32_t>(state.gamepad.buttons);
		for (const auto& id : PollableInputs()) {
			if (id.device != Device::Gamepad) {
				continue;
			}
			bool down = false;
			if (id.code == pad::kLT) {
				down = state.gamepad.leftTrigger >= kTriggerThreshold;
			} else if (id.code == pad::kRT) {
				down = state.gamepad.rightTrigger >= kTriggerThreshold;
			} else {
				down = (buttons & id.code) != 0;
			}
			if (down) {
				a_out.push_back(id);
			}
		}
	}
}
