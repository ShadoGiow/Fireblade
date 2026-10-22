#include "BindingEngine.h"

#include <algorithm>

namespace sm
{
	namespace
	{
		bool Contains(const std::vector<InputId>& a_set, const InputId& a_id)
		{
			return std::find(a_set.begin(), a_set.end(), a_id) != a_set.end();
		}

		bool AllHeld(const std::vector<InputId>& a_chord, const std::vector<InputId>& a_down)
		{
			return std::ranges::all_of(a_chord, [&](const InputId& id) { return Contains(a_down, id); });
		}

		// Drops from the quarantine whatever has been released, and returns a_down without the rest.
		std::vector<InputId> Filter(std::vector<InputId>& a_quarantine, const std::vector<InputId>& a_down)
		{
			std::erase_if(a_quarantine, [&](const InputId& q) { return !Contains(a_down, q); });
			std::vector<InputId> out;
			out.reserve(a_down.size());
			for (const auto& d : a_down) {
				if (!Contains(a_quarantine, d)) {
					out.push_back(d);
				}
			}
			return out;
		}
	}

	// ---------------------------------------------------------------- BindingEngine

	void BindingEngine::SetBindings(const Bindings& a_bindings)
	{
		_bindings = a_bindings;
		Reset();
	}

	void BindingEngine::Reset()
	{
		_runtime = {};
		_quarantine.clear();
		_snapshotQuarantine = true;
	}

	void BindingEngine::Update(std::uint64_t a_nowMs, const std::vector<InputId>& a_down, std::vector<Action>& a_fired)
	{
		if (_snapshotQuarantine) {
			_quarantine = a_down;
			_snapshotQuarantine = false;
		}
		const auto down = Filter(_quarantine, a_down);

		std::array<bool, kActionCount> fired{};
		// Press candidates are resolved by specificity at the end (largest chord wins).
		std::array<bool, kActionCount> pressCandidate{};

		for (std::size_t i = 0; i < kActionCount; ++i) {
			const auto& b = _bindings[i];
			auto&       rt = _runtime[i];

			if (!b.enabled || !b.IsValid()) {
				rt = {};
				continue;
			}

			const bool          held = AllHeld(b.inputs, down);
			const std::uint32_t ms = b.EffectiveMs();

			if (!rt.chordHeld && held) {
				// chord just completed
				rt.chordHeld = true;
				rt.chordSince = a_nowMs;
				rt.holdFired = false;
				rt.firedOnThisPress = false;

				switch (b.trigger) {
				case Trigger::Press:
					pressCandidate[i] = true;
					rt.firedOnThisPress = true;
					break;
				case Trigger::DoubleTap:
					if (rt.tapValid && a_nowMs - rt.tapReleasedAt <= ms) {
						fired[i] = true;
						rt.firedOnThisPress = true;
					}
					rt.tapValid = false;
					break;
				default:
					break;
				}
			} else if (rt.chordHeld && held) {
				if (b.trigger == Trigger::Hold && !rt.holdFired && a_nowMs - rt.chordSince >= ms) {
					fired[i] = true;
					rt.holdFired = true;
				}
			} else if (rt.chordHeld && !held) {
				// chord just broken
				rt.chordHeld = false;
				const auto heldFor = a_nowMs - rt.chordSince;
				if (b.trigger == Trigger::Tap && heldFor <= ms) {
					fired[i] = true;
				}
				if (b.trigger == Trigger::DoubleTap) {
					if (!rt.firedOnThisPress && heldFor <= ms) {
						rt.tapValid = true;
						rt.tapReleasedAt = a_nowMs;
					} else {
						rt.tapValid = false;
					}
				}
			}
		}

		// Specificity: if several Press bindings completed at the same instant, only the ones with the
		// largest chord fire (so "A" and "A + B" pressed together do not both trigger).
		std::size_t maxSize = 0;
		for (std::size_t i = 0; i < kActionCount; ++i) {
			if (pressCandidate[i]) {
				maxSize = std::max(maxSize, _bindings[i].inputs.size());
			}
		}
		for (std::size_t i = 0; i < kActionCount; ++i) {
			if (pressCandidate[i] && _bindings[i].inputs.size() == maxSize) {
				fired[i] = true;
			}
		}

		for (std::size_t i = 0; i < kActionCount; ++i) {
			if (fired[i]) {
				a_fired.push_back(static_cast<Action>(i));
			}
		}
	}

	// ---------------------------------------------------------------- ChordCapture

	void ChordCapture::Begin(std::uint64_t a_nowMs, std::uint32_t a_timeoutMs)
	{
		_state = State::Waiting;
		_chord.clear();
		_pressed.clear();
		_quarantine.clear();
		_snapshotQuarantine = true;
		_beganAt = a_nowMs;
		_timeoutMs = a_timeoutMs;
	}

	void ChordCapture::Cancel() noexcept
	{
		if (Active()) {
			_state = State::Cancelled;
		}
	}

	void ChordCapture::Acknowledge() noexcept
	{
		_state = State::Idle;
		_chord.clear();
		_pressed.clear();
		_quarantine.clear();
	}

	void ChordCapture::Update(std::uint64_t a_nowMs, const std::vector<InputId>& a_down)
	{
		if (!Active()) {
			return;
		}
		if (_snapshotQuarantine) {
			_quarantine = a_down;
			_snapshotQuarantine = false;
			_beganAt = a_nowMs;
		}
		_pressed = Filter(_quarantine, a_down);

		if (std::ranges::any_of(_pressed, [](const InputId& id) { return IsUnbindable(id); })) {
			_state = State::Cancelled;
			_chord.clear();
			return;
		}

		for (const auto& id : _pressed) {
			if (_chord.size() < kMaxChord && !Contains(_chord, id)) {
				_chord.push_back(id);
			}
		}

		if (!_pressed.empty()) {
			_state = State::Capturing;
		} else if (!_chord.empty()) {
			_state = State::Done;
		} else if (a_nowMs - _beganAt >= _timeoutMs) {
			_state = State::TimedOut;
		}
	}

	// ---------------------------------------------------------------- mode logic

	Command ResolveCommand(const std::vector<Action>& a_fired, bool a_active, bool a_toggleWithEnter) noexcept
	{
		const auto has = [&](Action a) { return std::find(a_fired.begin(), a_fired.end(), a) != a_fired.end(); };

		if (a_toggleWithEnter) {
			if (has(Action::Enter)) {
				return a_active ? Command::Exit : Command::Enter;
			}
		} else {
			if (a_active && has(Action::Exit)) {
				return Command::Exit;
			}
			if (!a_active && has(Action::Enter)) {
				return Command::Enter;
			}
		}

		if (has(Action::NextPack)) {
			return Command::NextPack;
		}
		if (has(Action::PrevPack)) {
			return Command::PrevPack;
		}
		return Command::None;
	}

	int CycleIndex(int a_count, int a_current, int a_direction) noexcept
	{
		if (a_count <= 0) {
			return -1;
		}
		if (a_current < 0 || a_current >= a_count) {
			return a_direction >= 0 ? 0 : a_count - 1;
		}
		const int next = (a_current + (a_direction >= 0 ? 1 : -1)) % a_count;
		return next < 0 ? next + a_count : next;
	}
}
