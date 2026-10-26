#include "pch.h"
#include "Stabilize.h"
#include "HavokString.h"
#include "Log.h"
#include "RE/B/BSAnimationGraphManager.h"
#include "RE/B/BShkbAnimationGraph.h"
#include "RE/H/hkbGenerator.h"
#include "RE/H/hkbGeneratorOutput.h"
#include "RE/H/hkQsTransform.h"
#include <xmmintrin.h>
#include <cstddef>

namespace bf::stabilize {
namespace {
    constexpr std::size_t kRoot=0, kCOM=4, kSpine=24;
    std::atomic<float> gWeight{0.0f};
    std::array<std::atomic<const RE::hkbCharacter*>,8> gCharacters{};
    std::atomic<bool> gApplied{false}, gRejected{false};
    struct Q { float x,y,z,w; };
    static_assert(sizeof(Q)==16);
    Q Mul(Q a,Q b) {
        return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
                a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
                a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
                a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
    }
    Q Inverse(Q a) { return {-a.x,-a.y,-a.z,a.w}; }
    bool Normalize(Q& q) {
        const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
        if (!std::isfinite(n) || n<0.000001f) return false;
        const float f=1.0f/std::sqrt(n);q.x*=f;q.y*=f;q.z*=f;q.w*=f;return true;
    }
    bool Read(const RE::hkQsTransform& t,Q& q) {
        _mm_storeu_ps(&q.x,t.rotation.vec.quad);return Normalize(q);
    }
    Q Slerp(Q a,Q b,float w) {
        float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
        if (dot<0.0f) { b={-b.x,-b.y,-b.z,-b.w};dot=-dot; }
        dot=std::clamp(dot,0.0f,1.0f);
        float x=1.0f-w,y=w;
        if (dot<0.9995f) {
            const float angle=std::acos(dot),s=std::sin(angle);
            x=std::sin((1.0f-w)*angle)/s;y=std::sin(w*angle)/s;
        }
        Q q{a.x*x+b.x*y,a.y*x+b.y*y,a.z*x+b.z*y,a.w*x+b.w*y};Normalize(q);return q;
    }
    RE::hkQsTransform* Pose(const RE::hkbGeneratorOutput* output) {
        if (!output || !output->tracks) return nullptr;
        const auto& tracks=*output->tracks;
        const auto& master=tracks.masterHeader;
        if (master.numTracks<3 || master.numTracks>64 || master.numBytes<64 || master.numBytes>1048576) return nullptr;
        const auto& header=tracks.trackHeaders[2]; // TRACK_POSE in Skyrim 2010.2.
        const auto flags=static_cast<std::uint8_t>(header.flags);
        if (flags!=0 || header.type!=RE::hkbGeneratorOutput::TrackType::kQsTransform ||
            header.elementSizeBytes!=sizeof(RE::hkQsTransform) || header.numData<=kSpine ||
            header.capacity<header.numData || header.onFraction<=0.0f) return nullptr;
        const auto offset=static_cast<std::uint16_t>(header.dataOffset);
        const auto tableEnd=16u+static_cast<unsigned>(master.numTracks)*16u;
        const auto end=static_cast<std::size_t>(offset)+static_cast<std::size_t>(header.numData)*sizeof(RE::hkQsTransform);
        if (offset<tableEnd || end>static_cast<std::size_t>(master.numBytes)) return nullptr;
        return reinterpret_cast<RE::hkQsTransform*>(reinterpret_cast<std::byte*>(output->tracks)+offset);
    }
    std::string Describe(const RE::hkbGeneratorOutput* output) {
        if (!output || !output->tracks) return "no tracks";
        const auto& m=output->tracks->masterHeader;
        if (m.numTracks<3 || m.numTracks>64 || m.numBytes<64 || m.numBytes>1048576)
            return std::format("invalid master: tracks={} bytes={}",m.numTracks,m.numBytes);
        const auto& h=output->tracks->trackHeaders[2];
        return std::format("n={} cap={} stride={} flags={} type={} on={} off={} bytes={}",
            h.numData,h.capacity,h.elementSizeBytes,static_cast<int>(h.flags),static_cast<int>(h.type),h.onFraction,
            static_cast<std::uint16_t>(h.dataOffset),m.numBytes);
    }
    bool Scoped(const RE::hkbCharacter* character) {
        if (!character) return false;
        for (auto& p:gCharacters) if (p.load(std::memory_order_acquire)==character) return true;
        return false;
    }
    // Actual generate ABI includes all four arguments after this (CommonLib's
    // abbreviated virtual declaration names only context). Havok slot is 0x17.
    using GenerateFn=void(RE::hkbGenerator*,const RE::hkbContext&,
        const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float);
    REL::Relocation<GenerateFn> gOriginal;
    void Generate(RE::hkbGenerator* generator,const RE::hkbContext& context,
        const RE::hkbGeneratorOutput** children,RE::hkbGeneratorOutput& output,float timeOffset) {
        const float weight=gWeight.load(std::memory_order_acquire);
        bool ready=false;Q baseModel{},sourceModel{};
        const char* nodeName=nullptr;
        if (weight>0.0f && generator && Scoped(context.character))
            nodeName=havokstring::View(generator->name);
        const bool own=nodeName && std::strcmp(nodeName,"BFM_LocomotionUpperBodyOverlay_MaskedV2")==0;
        // Capture child data before original: Havok may recycle/alias a child buffer.
        if (own && children) {
            auto* base=Pose(children[0]);auto* source=Pose(children[1]);
            Q br,bc,bs,sr,sc,ss;
            if (base && source && Read(base[kRoot],br) && Read(base[kCOM],bc) && Read(base[kSpine],bs) &&
                Read(source[kRoot],sr) && Read(source[kCOM],sc) && Read(source[kSpine],ss)) {
                baseModel=Mul(Mul(br,bc),bs);sourceModel=Mul(Mul(sr,sc),ss);ready=true;
            }
        }
        gOriginal(generator,context,children,output,timeOffset);
        if (!own) return;
        auto* pose=Pose(&output);Q root,com;
        if (!ready || !pose || !Read(pose[kRoot],root) || !Read(pose[kCOM],com)) {
            if (!gRejected.exchange(true)) Say("Stabilizzazione FIX10: traccia non compatibile, nessuna modifica; base=[{}] clip=[{}] out=[{}]",
                Describe(children ? children[0] : nullptr),Describe(children ? children[1] : nullptr),Describe(&output));
            return;
        }
        // Blend orientations in model space, then undo ONLY the live parent on Spine.
        // Root, COM, pelvis, legs, translations and root motion remain byte-identical.
        Q corrected=Mul(Inverse(Mul(root,com)),Slerp(baseModel,sourceModel,weight));
        if (!Normalize(corrected)) return;
        pose[kSpine].rotation.vec.quad=_mm_setr_ps(corrected.x,corrected.y,corrected.z,corrected.w);
        if (!gApplied.exchange(true)) Say("Stabilizzazione FIX10: compensazione COM applicata al solo busto del giocatore");
    }
}
void Install() {
    static bool installed=false;if (installed) return;
    REL::Relocation<std::uintptr_t> table{RE::VTABLE_hkbBlenderGenerator[0]};
    gOriginal=table.write_vfunc(0x17,Generate);installed=true;
    Say("Stabilizzazione FIX10: hook generate installato, filtro sul solo nodo BFM del giocatore");
}
void Reset() {
    gWeight.store(0.0f,std::memory_order_release);
    for (auto& c:gCharacters) c.store(nullptr,std::memory_order_release);
    gApplied=false;gRejected=false;
}
void SetWeight(RE::PlayerCharacter* player,float weight) {
    weight=std::clamp(weight,0.0f,1.0f);
    if (!player || weight<=0.0f) { gWeight.store(0.0f,std::memory_order_release);return; }
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if (!player->GetAnimationGraphManager(manager) || !manager) { gWeight=0.0f;return; }
    RE::BSSpinLockGuard lock(manager->GetRuntimeData().updateLock);
    std::size_t count=0;
    for (const auto& graph:manager->graphs) {
        if (!graph || graph->holder!=player || count==gCharacters.size()) continue;
        // Protect the hard-coded humanoid indices used by our existing Nemesis masks.
        if (graph->boneNodes.size()<=kSpine || !graph->boneNodes[kSpine].node ||
            graph->boneNodes[kSpine].node->name!="NPC Spine [Spn0]") continue;
        gCharacters[count++].store(&graph->characterInstance,std::memory_order_release);
    }
    for (std::size_t i=count;i<gCharacters.size();++i) gCharacters[i]=nullptr;
    gWeight.store(count ? weight : 0.0f,std::memory_order_release);
    if (!count && !gRejected.exchange(true)) Say("Stabilizzazione FIX10: schema del grafo giocatore non riconosciuto; applicazione saltata");
}
}
