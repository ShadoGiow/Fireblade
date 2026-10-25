#include "pch.h"
#include "Trails.h"
#include <cctype>
#include "Mode.h"
#include "Settings.h"
#include "Log.h"
#include "RE/B/BGSArtObject.h"
#include "RE/M/ModelReferenceEffect.h"
#include "RE/P/ProcessLists.h"

namespace bf::trails {
namespace {
    enum Command { None, Stop, Start };
    std::atomic<int> gCommand{None};
    RE::NiPointer<RE::ModelReferenceEffect> gEffect;
    RE::NiPointer<RE::NiAVObject> gActorRoot;
    RE::BGSArtObject* gArt = nullptr;
    RE::ObjectRefHandle gTarget{};
    bool gSpawnPending = false;
    RE::NiPointer<RE::ModelReferenceEffect> FindOwnEffect() {
        RE::NiPointer<RE::ModelReferenceEffect> result;
        if (auto* lists=RE::ProcessLists::GetSingleton(); lists && gArt) {
            lists->ForEachModelEffect([&](RE::ModelReferenceEffect* effect) {
                if (effect->artObject==gArt && effect->target==gTarget && !effect->finished) {
                    result.reset(effect); // Actual NiObject, retained while the manager lock is held.
                    return RE::BSContainer::ForEachResult::kStop;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        return result;
    }
    bool gSwing = false;
    bool gPending = false;
    float gLastSwingAt = -1000.0f;
    float gClock = 0.0f, gEndAt = 0.0f, gPendingUntil = 0.0f, gRetryAt = 0.0f;
    bool gMissingLogged = false;
    std::chrono::steady_clock::time_point gLast{};

    void ClearEffect() {
        if (!gEffect && gSpawnPending) gEffect=FindOwnEffect();
        gSpawnPending=false;
        if (gEffect) {
            // Own effect only: never StopArtObject by form or stop somebody else's effects.
            if (auto* root = gEffect->Get3D()) root->SetAppCulled(true);
            gEffect->finished = true;
            gEffect.reset();
        }
        gActorRoot.reset();
        gSwing = gPending = false;
    }
    void StopSwing() {
        if (gEffect && !gEffect->finished && gSwing)
            gEffect->NotifyAnimationGraph("end");
        gSwing = gPending = false;
    }
    bool Starts(std::string_view s) {
        return s == "weaponswing" || s == "weaponrightswing" || s == "collision_attackstart";
    }
    bool Ends(std::string_view s) {
        return s == "attackstop" || s == "attackend" || s == "collision_attackend" ||
            s == "beginweaponsheathe" || s == "weaponsheathe" || s == "unequip";
    }
}
void OnAnimationEvent(std::string_view tag) {
    if (tag.size() > 96) return;
    std::array<char, 97> lower{};
    for (std::size_t i=0; i<tag.size(); ++i)
        lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(tag[i])));
    const std::string_view key(lower.data(), tag.size());
    // Last command wins. Preserves end->start ordering in fast combos.
    if (Starts(key)) gCommand.store(Start);
    else if (Ends(key)) gCommand.store(Stop);
}
void Reset() {
    gCommand = None;
    ClearEffect();
    gArt = nullptr;
    gTarget = {};
    gClock = gEndAt = gPendingUntil = gRetryAt = 0.0f;
    gMissingLogged = false;
    gLastSwingAt = -1000.0f;
    gLast = {};
}
void Tick() {
    const auto now = std::chrono::steady_clock::now();
    const float dt = gLast.time_since_epoch().count() ?
        std::clamp(std::chrono::duration<float>(now-gLast).count(), 0.0f, 0.25f) : 0.0f;
    gLast = now;
    if (auto* ui=RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
        gCommand = None; // Never replay an old input after a paused menu.
        return;
    }
    gClock += dt;
    const int command = gCommand.exchange(None);
    auto* pc=RE::PlayerCharacter::GetSingleton();
    if (!S.swingTrails || !pc || !mode::AllowSwingTrails()) { ClearEffect(); return; }
    auto* actorRoot=pc->Get3D(false);
    if (!actorRoot) { ClearEffect(); return; }
    if ((gEffect && gEffect->finished) || (gActorRoot && gActorRoot.get()!=actorRoot)) ClearEffect();
    if (!gArt) {
        if (auto* data=RE::TESDataHandler::GetSingleton())
            gArt=data->LookupForm<RE::BGSArtObject>(0x802, "BladeFireMode.esp");
        if (!gArt) {
            if (!gMissingLogged) { Say("Swing FIRE: ARTO 0x802 assente; installare ESP aggiornato insieme alla DLL"); gMissingLogged=true; }
            return;
        }
    }
    if (!gEffect && gSpawnPending) {
        gEffect=FindOwnEffect();
        if (gEffect) gSpawnPending=false;
    }
    if (!gEffect && !gSpawnPending && gClock>=gRetryAt) {
        gRetryAt = gClock+0.50f;
        gTarget=pc->CreateRefHandle();
        gActorRoot.reset(actorRoot);
        // SE 1.5.97 returned 0x1 here in the captured crash, despite the pointer signature.
        // Invoke outside the manager lock. Never retain or dereference the return value.
        pc->ApplyArtObject(gArt, -1.0f);
        gSpawnPending=true;
        gEffect=FindOwnEffect();
        if (gEffect) gSpawnPending=false;
    }
    if (command==Stop) StopSwing();
    else if (command==Start && (!gSwing || gClock-gLastSwingAt>=0.08f)) {
        gPending=true;
        gPendingUntil=gClock+0.20f;
    }
    if (gPending && gEffect && !gEffect->finished && gEffect->Get3D() && gEffect->GetAttached()) {
        if (gSwing) gEffect->NotifyAnimationGraph("end");
        if (gEffect->NotifyAnimationGraph("weaponswing")) {
            gSwing=true;
            gLastSwingAt=gClock;
            gEndAt=gClock+std::clamp(S.swingTrailSeconds, 0.10f, 1.50f);
            gPending=false;
        }
    }
    if (gPending && gClock>gPendingUntil) {
        gPending=false;
        if (S.logEvents) Say("Swing FIRE: effetto non pronto, evento scaduto senza riproduzione ritardata");
    }
    if (gSwing && gClock>=gEndAt) StopSwing();
}
}
