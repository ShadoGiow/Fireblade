#include "pch.h"
#include "Trails.h"
#include "TrailHistory.h"
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
    RE::NiPointer<RE::NiAVObject> gBoundModel;
    RE::NiPointer<RE::NiControllerManager> gManager;
    RE::NiPointer<RE::NiControllerSequence> gBegin, gLoop, gEnd;
    struct BladeSample { float time; RE::NiTransform transform; };
    std::deque<BladeSample> gHistory;
    std::array<RE::NiPointer<RE::NiAVObject>,6> gHistoryBones;
    bool gHistoryReady = false;
    RE::NiTransform Interpolate(const RE::NiTransform& a, const RE::NiTransform& b, float u) {
        RE::NiTransform out;
        out.translate=a.translate*(1.0f-u)+b.translate*u;
        out.scale=a.scale*(1.0f-u)+b.scale*u;
        RE::NiQuaternion qa(a.rotate), qb(b.rotate);
        // Shortest-arc normalized lerp; avoids matrix shearing of the blade.
        if (qa.Dot(qb)<0.0f) { qb.w=-qb.w;qb.x=-qb.x;qb.y=-qb.y;qb.z=-qb.z; }
        RE::NiQuaternion q(qa.w*(1-u)+qb.w*u,qa.x*(1-u)+qb.x*u,qa.y*(1-u)+qb.y*u,qa.z*(1-u)+qb.z*u);
        const float length=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
        if (length>0.0001f) { q.w/=length;q.x/=length;q.y/=length;q.z/=length;q.ToRotation(out.rotate); }
        else out.rotate=b.rotate;
        return out;
    }
    RE::NiTransform SampleBlade(float at) {
        if (at<=gHistory.front().time) return gHistory.front().transform;
        for (std::size_t i=1;i<gHistory.size();++i) {
            if (at<=gHistory[i].time) {
                const auto& a=gHistory[i-1];const auto& b=gHistory[i];
                return Interpolate(a.transform,b.transform,std::clamp((at-a.time)/std::max(b.time-a.time,0.00001f),0.0f,1.0f));
            }
        }
        return gHistory.back().transform;
    }
    bool UpdateRibbon(RE::NiAVObject* actorRoot, float time, bool reset) {
        if (!gHistoryReady || !actorRoot) return false;
        auto* weapon=actorRoot->GetObjectByName("WEAPON");
        if (!weapon) return false;
        if (reset || (!gHistory.empty() && time-gHistory.back().time>0.15f)) gHistory.clear();
        if (gHistory.empty() || time>gHistory.back().time+0.0001f) gHistory.push_back({time,weapon->world});
        const float span=std::clamp(S.swingTrailSeconds*0.30f,0.05f,0.16f);
        while (gHistory.size()>2 && gHistory[1].time<time-span-0.04f) gHistory.pop_front();
        for (std::size_t i=0;i<gHistoryBones.size();++i) {
            auto* bone=gHistoryBones[i].get();
            if (!bone || !bone->parent || std::abs(bone->parent->world.scale)<0.0001f) return false;
            const auto desired=SampleBlade(time-span*kHistoryFractions[i]);
            bone->local=bone->parent->world.Invert()*desired;
            bone->world=desired;
        }
        return true;
    }
    RE::NiControllerManager* FindManager(RE::NiAVObject* root, int depth=0) {
        if (!root || depth>20) return nullptr;
        for (auto* c=root->GetControllers(); c; c=c->next.get())
            if (auto* manager=c->AsNiControllerManager()) return manager;
        if (auto* node=root->AsNode())
            for (const auto& child:node->children)
                if (auto* manager=FindManager(child.get(),depth+1)) return manager;
        return nullptr;
    }
    void PlaySequence(RE::NiControllerSequence* sequence) {
        if (!sequence || !gManager) return;
        for (auto* other:{gBegin.get(),gLoop.get(),gEnd.get()})
            if (other && !other->Inactive()) other->Deactivate(0.0f,false);
        if (!gManager->flags.any(RE::NiTimeController::Flag::kActive)) gManager->Start(0.0f);
        sequence->frequency=1.0f;
        if (sequence==gLoop.get())
            sequence->frequency=0.5f/std::clamp(S.swingTrailSeconds,0.10f,1.50f);
        sequence->Activate(0,true,1.0f,0.0f,nullptr,false);
    }
    bool BindSequences() {
        if (!gEffect || gEffect->finished) return false;
        auto* root=gEffect->Get3D();
        if (!root) return false;
        if (gBoundModel.get()!=root || !gManager || !gBegin || !gLoop) {
            gBoundModel.reset(root);gManager.reset(FindManager(root));
            gHistory.clear();gHistoryReady=true;
            for (std::size_t i=0;i<gHistoryBones.size();++i) {
                const RE::BSFixedString name(std::format("BFM_History{}",i));
                gHistoryBones[i].reset(root->GetObjectByName(name));
                if (!gHistoryBones[i]) gHistoryReady=false;
            }
            gBegin.reset(gManager ? gManager->GetSequenceByName("mBegin") : nullptr);
            gLoop.reset(gManager ? gManager->GetSequenceByName("mLoop") : nullptr);
            gEnd.reset(gManager ? gManager->GetSequenceByName("mEnd") : nullptr);
            if (gBegin && gLoop) {
                PlaySequence(gBegin.get());
                Say("Swing FIRE FIX5: sequenze pronte, history bones={}",gHistoryReady);
            } else Say("Swing FIRE FIX5: manager={} mBegin={} mLoop={}",bool(gManager),bool(gBegin),bool(gLoop));
        }
        return gManager && gBegin && gLoop && gHistoryReady;
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
        gHistory.clear();gHistoryReady=false;
        for (auto& bone:gHistoryBones) bone.reset();
        gBegin.reset();gLoop.reset();gEnd.reset();gManager.reset();gBoundModel.reset();
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
        if (gEffect && !gEffect->finished && gSwing && gBegin)
            PlaySequence(gBegin.get());
        gSwing = gPending = false;
        gHistory.clear();
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
    // Clean persisted instances of this private ARTO from earlier saved-game versions.
    if (auto* pc=RE::PlayerCharacter::GetSingleton()) {
        if (auto* data=RE::TESDataHandler::GetSingleton()) {
            auto* art=data->LookupForm<RE::BGSArtObject>(0x802,"BladeFireMode.esp");
            const auto handle=pc->CreateRefHandle();
            if (auto* lists=RE::ProcessLists::GetSingleton(); lists && art) {
                lists->ForEachModelEffect([&](RE::ModelReferenceEffect* effect) {
                    if (effect->artObject==art && effect->target==handle && !effect->finished) {
                        if (auto* root=effect->Get3D()) root->SetAppCulled(true);
                        effect->finished=true;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
        }
    }
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
    if (!S.swingTrails || !pc || !mode::AllowSwingTrails(true)) { ClearEffect(); return; }
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
    const bool ready=BindSequences();
    if (!mode::AllowSwingTrails()) { StopSwing(); return; } // Prewarm invisibly during opening.
    if (command==Stop) StopSwing();
    else if (command==Start && (!gSwing || gClock-gLastSwingAt>=0.08f)) {
        gPending=true;
        gPendingUntil=gClock+0.20f;
    }
    if (gPending && ready && UpdateRibbon(actorRoot,gClock,true)) {
        PlaySequence(gLoop.get());
        {
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
    if (gSwing && (gClock>=gEndAt || !UpdateRibbon(actorRoot,gClock,false))) StopSwing();
}
}
