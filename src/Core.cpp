#include "Common.h"

namespace ESA
{
	Forms      F;
	InputState In;

	namespace
	{
		using Clock = std::chrono::steady_clock;
		const Clock::time_point kT0 = Clock::now();

		double Now()
		{
			return std::chrono::duration<double>(Clock::now() - kT0).count();
		}

		// ---------------------------------------------------------------- logging / status
		std::mutex              g_logMx;
		std::deque<std::string> g_log;

		std::mutex g_stMx;
		Status     g_status;

		// ---------------------------------------------------------------- scheduling (main thread only)
		struct Timer
		{
			double                fireAt;
			std::function<void()> fn;
		};
		std::vector<Timer> g_timers;

		void After(double a_seconds, std::function<void()> a_fn)
		{
			g_timers.push_back({ Now() + a_seconds, std::move(a_fn) });
		}

		void Defer(std::function<void()> a_fn)
		{
			SKSE::GetTaskInterface()->AddTask(std::move(a_fn));
		}

		// ---------------------------------------------------------------- state
		bool g_applied[kMarkerCount]{};
		bool g_desired[kMarkerCount]{};
		bool g_lastStance = false;

		std::atomic<bool> g_resync{ true };
		std::atomic<bool> g_ready{ false };
		std::atomic<bool> g_started{ false };
		std::atomic<bool> g_tickQueued{ false };
		std::atomic<bool> g_dirty{ false };
		std::atomic<int>  g_action{ 0 };

		int    g_tickCount = 0;
		double g_nextReign = 0.0;
		double g_lastChase = -10.0;

		std::unordered_map<RE::FormID, bool> g_fireCache;

		constexpr RE::FormID kMarkerIds[kMarkerCount][2] = {
			{ 0xE80, 0xE81 }, { 0xE82, 0xE83 }, { 0xE84, 0xE85 }, { 0xE86, 0xE87 },
			{ 0xE88, 0xE89 }, { 0xE8A, 0xE8B }, { 0xE8C, 0xE8D }
		};

		constexpr RE::FormID kXMarkerBase = 0x0000003B;
		constexpr RE::FormID kKeywordMagicDamageFire = 0x0001CEAD;

		template <class T>
		T* Lookup(RE::FormID a_localId)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return (dh && !S.pluginFile.empty()) ? dh->LookupForm<T>(a_localId, S.pluginFile) : nullptr;
		}

		// ---------------------------------------------------------------- helpers
		void CastSelf(RE::Actor* a_actor, RE::SpellItem* a_spell)
		{
			if (!a_actor || !a_spell) {
				return;
			}
			if (auto* mc = a_actor->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
				mc->CastSpellImmediate(a_spell, false, a_actor, 1.0f, false, 0.0f, a_actor);
			}
		}

		bool IsFireSpell(RE::SpellItem* a_spell)
		{
			if (!a_spell) {
				return false;
			}
			if (const auto it = g_fireCache.find(a_spell->GetFormID()); it != g_fireCache.end()) {
				return it->second;
			}
			bool fire = false;
			for (auto* eff : a_spell->effects) {
				if (!eff || !eff->baseEffect) {
					continue;
				}
				auto* me = eff->baseEffect;
				if (me->HasKeywordID(kKeywordMagicDamageFire) || me->data.resistVariable == RE::ActorValue::kResistFire) {
					fire = true;
					break;
				}
			}
			g_fireCache[a_spell->GetFormID()] = fire;
			return fire;
		}

		bool ComputeStance(RE::PlayerCharacter* a_pc, Status& a_st)
		{
			a_st.bladeEquipped = false;
			a_st.leftIsSpell = false;
			a_st.leftIsFire = false;
			a_st.rightName.clear();
			a_st.leftName.clear();

			auto* right = a_pc->GetEquippedObject(false);
			auto* weap = right ? right->As<RE::TESObjectWEAP>() : nullptr;
			if (weap) {
				if (const char* n = weap->GetName()) {
					a_st.rightName = n;
				}
				a_st.bladeEquipped = F.kwBlade && weap->HasKeyword(F.kwBlade);
			}

			RE::SpellItem* spell = nullptr;
			if (auto* left = a_pc->GetEquippedObject(true)) {
				spell = left->As<RE::SpellItem>();
				if (!spell) {
					if (const char* n = left->GetName()) {
						a_st.leftName = n;
					}
				}
			}
			if (!spell) {
				if (auto* sel = a_pc->GetActorRuntimeData().selectedSpells[RE::Actor::SlotTypes::kLeftHand]) {
					spell = sel->As<RE::SpellItem>();
				}
			}
			if (spell) {
				a_st.leftIsSpell = true;
				if (const char* n = spell->GetName()) {
					a_st.leftName = n;
				}
				a_st.leftIsFire = IsFireSpell(spell);
			}

			if (!a_st.bladeEquipped) {
				return false;
			}
			switch (static_cast<StanceMode>(S.stanceMode)) {
			case StanceMode::AnyHand:
				return true;
			case StanceMode::AnySpell:
				return a_st.leftIsSpell;
			case StanceMode::Destruction:
				return spell && spell->GetAssociatedSkill() == RE::ActorValue::kDestruction;
			case StanceMode::FireSpell:
			default:
				return a_st.leftIsSpell && a_st.leftIsFire;
			}
		}

		bool StickNeutral(float a_x, float a_y)
		{
			return std::sqrt(a_x * a_x + a_y * a_y) < S.deadzone;
		}

		void SyncMarkers(RE::PlayerCharacter* a_pc)
		{
			for (int i = 0; i < kMarkerCount; ++i) {
				auto* sp = F.marker[i].spell;
				if (!sp) {
					continue;
				}
				if (g_desired[i] != g_applied[i]) {
					if (g_desired[i]) {
						a_pc->AddSpell(sp);
					} else {
						a_pc->RemoveSpell(sp);
					}
					g_applied[i] = g_desired[i];
					Vlogf("marker {} -> {}", kMarkerNames[i], g_desired[i] ? "ON" : "off");
				}
			}
		}

		void ResyncFromPlayer(RE::PlayerCharacter* a_pc)
		{
			for (int i = 0; i < kMarkerCount; ++i) {
				if (auto* sp = F.marker[i].spell) {
					g_applied[i] = a_pc->HasSpell(sp);
				}
			}
		}

		void RemoveAllMarkers(RE::PlayerCharacter* a_pc)
		{
			for (int i = 0; i < kMarkerCount; ++i) {
				if (auto* sp = F.marker[i].spell) {
					a_pc->RemoveSpell(sp);
				}
				g_applied[i] = false;
				g_desired[i] = false;
			}
		}

		// ---------------------------------------------------------------- gameplay actions
		void DoReignition(RE::PlayerCharacter* a_pc)
		{
			if (F.spReign) {
				CastSelf(a_pc, F.spReign);
				Logf("Re-Ignition lanciata");
			}
		}

		void MaybeReignition(RE::PlayerCharacter* a_pc, Status& a_st, double a_now)
		{
			auto* av = a_pc->AsActorValueOwner();
			if (!av) {
				return;
			}
			const float hp = av->GetActorValue(RE::ActorValue::kHealth);
			const float mx = std::max(av->GetPermanentActorValue(RE::ActorValue::kHealth), hp);
			a_st.healthPct = mx > 0.0f ? (hp / mx) * 100.0f : 0.0f;
			a_st.magicka = av->GetActorValue(RE::ActorValue::kMagicka);

			auto* mt = a_pc->AsMagicTarget();
			a_st.reignCooldown = F.mgReignCd && mt && mt->HasMagicEffect(F.mgReignCd);

			if (!S.reignEnabled || !F.spReign || a_now < g_nextReign) {
				return;
			}
			g_nextReign = a_now + 0.25;
			if (!a_st.bladeEquipped || hp <= 0.0f || a_pc->IsDead()) {
				return;
			}
			if (S.reignNeedCombat && !a_pc->IsInCombat()) {
				return;
			}
			if (a_st.reignCooldown || a_st.healthPct > static_cast<float>(S.reignPercent)) {
				return;
			}
			DoReignition(a_pc);
		}

		void DoChain()
		{
			// E28 (marked target reached 3 stacks) -> the player receives E29: chase / combo / execute permissions
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (pc && F.spChain) {
				CastSelf(pc, F.spChain);
				Logf("catena marchi completa -> permessi chase/combo/esecuzione");
			}
		}

		void DoChase()
		{
			if (!S.chaseAuto) {
				return;
			}
			const double now = Now();
			if (now - g_lastChase < 1.5) {
				return;
			}
			g_lastChase = now;
			After(S.chaseDelay, [] {
				auto* pc = RE::PlayerCharacter::GetSingleton();
				if (!pc || !pc->Is3DLoaded() || pc->IsDead() || !g_lastStance) {
					return;
				}
				auto* st = pc->AsActorState();
				if (!st || !st->IsWeaponDrawn()) {
					return;
				}
				if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
					return;
				}
				pc->NotifyAnimationGraph("attackPowerStartInPlace");
				Logf("chase: inviato attackPowerStartInPlace");
			});
		}

		void PlaceIncineration(float a_dist, bool a_first)
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!pc || !F.spIncin) {
				return;
			}
			const float yaw = pc->GetAngleZ();
			const auto  p = pc->GetPosition();
			const RE::NiPoint3 pos{ p.x + std::sin(yaw) * a_dist, p.y + std::cos(yaw) * a_dist, p.z };

			RE::NiPointer<RE::TESObjectREFR> marker;
			if (F.xmarker) {
				marker = pc->PlaceObjectAtMe(F.xmarker, false);
			}
			RE::MagicCaster* mc = nullptr;
			if (marker) {
				marker->SetPosition(pos);
				mc = marker->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
			}
			if (mc) {
				mc->CastSpellImmediate(F.spIncin, false, marker.get(), 1.0f, false, 0.0f, pc);
				Vlogf("incenerimento a {:.0f} unita'", a_dist);
			} else if (a_first) {
				// no caster available on a placed marker: the AoE is centred on the player instead
				CastSelf(pc, F.spIncin);
				Logf("incenerimento: marker senza caster, uso il giocatore");
			}
			if (marker) {
				auto h = marker->CreateRefHandle();
				After(2.0, [h]() mutable {
					if (auto r = h.get()) {
						r->Disable();
						r->SetDelete(true);
					}
				});
			}
		}

		void DoIncinerate()
		{
			if (!S.incEnabled) {
				return;
			}
			const int total = 1 + std::max(0, S.incExtra);
			for (int i = 0; i < total; ++i) {
				const float dist = S.incDist + S.incStep * static_cast<float>(i);
				const bool  first = (i == 0);
				if (i == 0) {
					PlaceIncineration(dist, first);
				} else {
					After(static_cast<double>(S.incDelay) * i, [dist, first] { PlaceIncineration(dist, first); });
				}
			}
		}

		void DoBurst(RE::ObjectRefHandle a_target)
		{
			if (!S.burstEnabled || !F.explBurst) {
				return;
			}
			auto t = a_target.get();
			if (!t) {
				return;
			}
			t->PlaceObjectAtMe(F.explBurst, false);
			Logf("esplosione di fuoco sul bersaglio");
		}

		void DoGhostRemoval()
		{
			if (S.ghostBlade || !F.spGhost) {
				return;
			}
			After(0.15, [] {
				if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
					pc->RemoveSpell(F.spGhost);
				}
			});
		}

		void RunAction(RE::PlayerCharacter* a_pc, Action a_act)
		{
			switch (a_act) {
			case Action::RemoveMarkers:
				RemoveAllMarkers(a_pc);
				Logf("tutti i marker rimossi");
				break;
			case Action::Resync:
				ResyncFromPlayer(a_pc);
				Logf("marker risincronizzati dal giocatore");
				break;
			case Action::TestReignition:
				DoReignition(a_pc);
				break;
			case Action::TestChase:
				g_lastChase = -10.0;
				g_lastStance = true;
				DoChase();
				break;
			case Action::TestIncinerate:
				DoIncinerate();
				break;
			case Action::TestBurst:
				{
					// first hostile target in front of us: use the combat target when there is one
					RE::ObjectRefHandle h;
					if (auto ct = a_pc->GetActorRuntimeData().currentCombatTarget.get()) {
						h = ct->GetHandle();
					} else {
						h = a_pc->GetHandle();
					}
					DoBurst(h);
				}
				break;
			default:
				break;
			}
		}
	}

	// ======================================================================== public
	void LogLine(const std::string& a_line)
	{
		SKSE::log::info("{}", a_line);
		std::lock_guard lk(g_logMx);
		g_log.push_back(a_line);
		while (g_log.size() > 60) {
			g_log.pop_front();
		}
	}

	std::vector<std::string> LogSnapshot()
	{
		std::lock_guard lk(g_logMx);
		return { g_log.begin(), g_log.end() };
	}

	Status StatusSnapshot()
	{
		std::lock_guard lk(g_stMx);
		return g_status;
	}

	void MarkSettingsDirty()
	{
		g_dirty = true;
	}

	void RequestAction(Action a_action)
	{
		g_action = static_cast<int>(a_action);
	}

	bool ResolveForms()
	{
		F = Forms{};
		bool ok = true;

		if (S.pluginFile.empty()) {
			Logf("TargetPlugin non impostato nell'INI: il plugin resta inattivo");
			return false;
		}

		for (int i = 0; i < kMarkerCount; ++i) {
			F.marker[i].mgef = Lookup<RE::EffectSetting>(kMarkerIds[i][0]);
			F.marker[i].spell = Lookup<RE::SpellItem>(kMarkerIds[i][1]);
			if (!F.marker[i].mgef || !F.marker[i].spell) {
				Logf("form mancante: marker {} ({:X}/{:X})", kMarkerNames[i], kMarkerIds[i][0], kMarkerIds[i][1]);
				ok = false;
			}
		}

		F.kwBlade = Lookup<RE::BGSKeyword>(0x84A);
		if (!F.kwBlade) {
			Logf("form mancante: keyword dell'arma (84A)");
			ok = false;
		}

		F.spReign = Lookup<RE::SpellItem>(0xE15);
		F.mgReignCd = Lookup<RE::EffectSetting>(0xE16);
		F.mgChase = Lookup<RE::EffectSetting>(0xE2A);
		F.mgChain = Lookup<RE::EffectSetting>(0xE28);
		F.spChain = Lookup<RE::SpellItem>(0xE29);
		F.mgIncin = Lookup<RE::EffectSetting>(0xE32);
		F.spIncin = Lookup<RE::SpellItem>(0xE34);
		F.mgBurst = Lookup<RE::EffectSetting>(0xE5A);
		F.explBurst = Lookup<RE::BGSExplosion>(0xE5B);
		F.mgGhost = Lookup<RE::EffectSetting>(0x866);
		F.spGhost = Lookup<RE::SpellItem>(0x864);
		F.xmarker = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarkerBase);

		auto report = [](const char* a_name, const void* a_p) {
			if (!a_p) {
				Logf("form opzionale mancante: {}", a_name);
			}
		};
		report("Re-Ignition (E15)", F.spReign);
		report("cooldown Re-Ignition (E16)", F.mgReignCd);
		report("chase (E2A)", F.mgChase);
		report("catena (E28/E29)", F.mgChain && F.spChain ? &F : nullptr);
		report("incenerimento (E32/E34)", F.mgIncin && F.spIncin ? &F : nullptr);
		report("esplosione (E5A/E5B)", F.mgBurst && F.explBurst ? &F : nullptr);
		report("lama fantasma (866/864)", F.mgGhost && F.spGhost ? &F : nullptr);
		report("XMarker", F.xmarker);

		F.ok = ok;
		Logf("form risolti: {}", ok ? "OK" : "ERRORE (plugin dati non caricato?)");
		return ok;
	}

	void OnGameLoading()
	{
		g_ready = false;
	}

	void OnGameLoaded()
	{
		g_timers.clear();
		g_fireCache.clear();
		In.padHeld = 0;
		In.lsx = 0.0f;
		In.lsy = 0.0f;
		In.kbW = In.kbA = In.kbS = In.kbD = false;
		std::fill(std::begin(g_desired), std::end(g_desired), false);
		g_resync = true;
		g_nextReign = 0.0;
		g_ready = true;
		Logf("partita caricata: sincronizzazione marker");
	}

	void Tick()
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !pc->Is3DLoaded() || !F.ok) {
			return;
		}
		const double now = Now();
		++g_tickCount;

		// timers
		if (!g_timers.empty()) {
			std::vector<std::function<void()>> due;
			for (auto it = g_timers.begin(); it != g_timers.end();) {
				if (it->fireAt <= now) {
					due.push_back(std::move(it->fn));
					it = g_timers.erase(it);
				} else {
					++it;
				}
			}
			for (auto& fn : due) {
				fn();
			}
		}

		if (g_resync.exchange(false)) {
			ResyncFromPlayer(pc);
		}

		if (const int act = g_action.exchange(0); act != 0) {
			RunAction(pc, static_cast<Action>(act));
		}

		Status st;
		const bool stance = S.enabled && ComputeStance(pc, st);
		g_lastStance = stance;
		st.stance = stance;

		// input
		const std::uint32_t pad = In.padHeld.load();
		const float         lx = In.lsx.load();
		const float         ly = In.lsy.load();
		st.padHeld = pad;
		st.lsx = lx;
		st.lsy = ly;

		bool ult = false;
		bool fwd = false;
		bool back = false;
		if (S.padEnabled) {
			if (S.ultChord != 0 && (pad & S.ultChord) == S.ultChord && (!S.ultNeutralStick || StickNeutral(lx, ly))) {
				ult = true;
			}
			if (S.padForwardComb && ly > S.deadzone) {
				fwd = true;
			}
			if (S.padBackExec && ly < -S.deadzone) {
				back = true;
			}
		}
		if (S.kbFallback) {
			if (In.kbA.load() && In.kbD.load()) {
				ult = true;
			}
			if (In.kbW.load()) {
				fwd = true;
			}
			if (In.kbS.load()) {
				back = true;
			}
		}
		st.ultHeld = ult;
		st.fwdHeld = fwd;
		st.backHeld = back;

		// resources / reignition share the actor value reads
		MaybeReignition(pc, st, now);
		const float mag = st.magicka;
		auto        enough = [&](int a_min) { return a_min <= 0 || mag >= static_cast<float>(a_min); };

		g_desired[kStance] = stance;
		g_desired[kUlt] = stance && ult;
		g_desired[kComb] = stance && fwd;
		g_desired[kExec] = stance && back;
		g_desired[kMagSkill] = stance && enough(S.magSkill);
		g_desired[kMagUltSkill] = stance && enough(S.magUltSkill);
		g_desired[kMagUlt] = stance && enough(S.magUlt);

		SyncMarkers(pc);

		// self-repair every ~3 s: if the spell list and our bookkeeping disagree, trust the player
		if (g_tickCount % 90 == 0) {
			ResyncFromPlayer(pc);
		}

		for (int i = 0; i < kMarkerCount; ++i) {
			st.desired[i] = g_desired[i];
			st.applied[i] = g_applied[i];
		}
		{
			std::lock_guard lk(g_stMx);
			g_status = std::move(st);
		}
	}

	void StartCore()
	{
		if (g_started.exchange(true)) {
			return;
		}
		std::thread([] {
			double lastSave = 0.0;
			for (;;) {
				std::this_thread::sleep_for(33ms);

				if (g_dirty.load() && Now() - lastSave > 1.0) {
					g_dirty = false;
					lastSave = Now();
					Ini::Save();
				}
				if (!g_ready.load()) {
					continue;
				}
				if (g_tickQueued.exchange(true)) {
					continue;
				}
				SKSE::GetTaskInterface()->AddTask([] {
					g_tickQueued = false;
					Tick();
				});
			}
		}).detach();
	}

	void OnMagicEffectApplied(RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_caster, RE::FormID a_effect)
	{
		if (!F.ok || !S.enabled || !g_ready.load()) {
			return;
		}
		auto is = [&](RE::EffectSetting* a_mgef) { return a_mgef && a_mgef->GetFormID() == a_effect; };

		if (is(F.mgChain)) {
			if (a_caster && a_caster->IsPlayerRef()) {
				Defer([] { DoChain(); });
			}
		} else if (is(F.mgChase)) {
			if (a_target && a_target->IsPlayerRef()) {
				Defer([] { DoChase(); });
			}
		} else if (is(F.mgIncin)) {
			if (a_target && a_target->IsPlayerRef()) {
				Defer([] { DoIncinerate(); });
			}
		} else if (is(F.mgBurst)) {
			if (a_target) {
				const auto h = a_target->CreateRefHandle();
				Defer([h] { DoBurst(h); });
			}
		} else if (is(F.mgGhost)) {
			if (a_target && a_target->IsPlayerRef()) {
				Defer([] { DoGhostRemoval(); });
			}
		}
	}
}
