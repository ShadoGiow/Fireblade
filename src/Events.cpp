#include "Common.h"

namespace ESA
{
	namespace
	{
		// DirectInput scancodes of the keyboard fallback (A+D / W / S as in the original key-trace setup)
		constexpr std::uint32_t kDikW = 0x11;
		constexpr std::uint32_t kDikA = 0x1E;
		constexpr std::uint32_t kDikS = 0x1F;
		constexpr std::uint32_t kDikD = 0x20;

		// XInput gives one bit per digital button; the two triggers arrive as ids 9 / 10 with an analog value
		std::uint32_t PadMaskFromId(std::uint32_t a_id)
		{
			switch (a_id) {
			case RE::BSWin32GamepadDevice::Keys::kLeftTrigger:
				return Btn::LT;
			case RE::BSWin32GamepadDevice::Keys::kRightTrigger:
				return Btn::RT;
			default:
				return (a_id != 0 && a_id < 0x10000 && (a_id & (a_id - 1)) == 0) ? a_id : 0;
			}
		}

		class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* Get()
			{
				static InputSink s;
				return std::addressof(s);
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_events) {
					return RE::BSEventNotifyControl::kContinue;
				}

				for (auto* e = *a_events; e; e = e->next) {
					switch (e->GetEventType()) {
					case RE::INPUT_EVENT_TYPE::kButton:
						HandleButton(e->AsButtonEvent());
						break;
					case RE::INPUT_EVENT_TYPE::kThumbstick:
						HandleStick(e->AsThumbstickEvent());
						break;
					default:
						break;
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			static void HandleButton(const RE::ButtonEvent* a_btn)
			{
				if (!a_btn) {
					return;
				}
				const auto id = a_btn->GetIDCode();

				switch (a_btn->GetDevice()) {
				case RE::INPUT_DEVICE::kGamepad:
					{
						const auto mask = PadMaskFromId(id);
						if (mask == 0) {
							return;
						}
						const bool analog = (mask == Btn::LT || mask == Btn::RT);
						const bool down = analog ? (a_btn->Value() > 0.5f) : a_btn->IsPressed();
						if (down) {
							In.padHeld.fetch_or(mask);
						} else {
							In.padHeld.fetch_and(~mask);
						}
					}
					break;

				case RE::INPUT_DEVICE::kKeyboard:
					{
						const bool down = a_btn->IsPressed();
						switch (id) {
						case kDikW: In.kbW = down; break;
						case kDikA: In.kbA = down; break;
						case kDikS: In.kbS = down; break;
						case kDikD: In.kbD = down; break;
						default: break;
						}
					}
					break;

				default:
					break;
				}
			}

			static void HandleStick(const RE::ThumbstickEvent* a_stick)
			{
				if (a_stick && a_stick->IsLeft()) {
					In.lsx = a_stick->xValue;
					In.lsy = a_stick->yValue;
				}
			}
		};

		class MagicSink final : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
		{
		public:
			static MagicSink* Get()
			{
				static MagicSink s;
				return std::addressof(s);
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* a_event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
			{
				if (a_event) {
					OnMagicEffectApplied(a_event->target.get(), a_event->caster.get(), a_event->magicEffect);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void RegisterSinks()
	{
		if (auto* im = RE::BSInputDeviceManager::GetSingleton()) {
			im->AddEventSink(InputSink::Get());
			Logf("sink input registrato");
		} else {
			Logf("BSInputDeviceManager non disponibile: input pad/tastiera non letto");
		}

		if (auto* sh = RE::ScriptEventSourceHolder::GetSingleton()) {
			sh->AddEventSink<RE::TESMagicEffectApplyEvent>(MagicSink::Get());
			Logf("sink effetti magici registrato");
		} else {
			Logf("ScriptEventSourceHolder non disponibile: nessun gestore effetti");
		}
	}
}
