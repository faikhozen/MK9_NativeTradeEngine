#include "stage_bounds_policy.hpp"
#include "toasty_lifecycle_policy.hpp"
#include "reversal_timing_policy.hpp"
#include "startup_skip_policy.hpp"
#include "roll_wakeup_policy.hpp"
#include "hit_outcome_policy.hpp"
#include "stage_visibility_policy.hpp"
#include "void_lighting_policy.hpp"
#include "void_render_policy.hpp"
#include "clean_fighters_policy.hpp"
#include "wakeup_classification.hpp"
#include "juggle_model.hpp"
#include "../src/input_recording.hpp"
#include "../src/recording_io.hpp"
#include "../src/executable_compatibility.hpp"
#include "../src/hook_pair.hpp"
#include "../src/profiler.hpp"
#include "../src/io_accounting.hpp"
#include "../src/frame_data.hpp"
#include "../src/attack_phase.hpp"
#include <thread>
#include <atomic>
#include <limits>
#include <vector>
#include <cstring>
#include <cstdlib>

using namespace MK9TradeFix;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static RecordingSlot Original() {
    RecordingSlot slot;
    slot.id = 5;
    strcpy_s(slot.name, "Keep this recording");
    strcpy_s(slot.characterName, "Scorpion");
    slot.frames.resize(2);
    slot.frames[0].dirToken = 123;
    slot.frames[1].btnMask = 456;
    slot.totalFrames = 2;
    slot.isEmpty = false;
    return slot;
}
static bool Unchanged(const RecordingSlot& slot) {
    return slot.id == 5 && strcmp(slot.name, "Keep this recording") == 0 &&
        strcmp(slot.characterName, "Scorpion") == 0 && slot.totalFrames == 2 && !slot.isEmpty &&
        slot.frames.size() == 2 && slot.frames[0].dirToken == 123 && slot.frames[1].btnMask == 456;
}
static std::vector<unsigned char> Encode() {
    FILE* file = tmpfile(); CHECK(file);
    CHECK(RecordingIO::Write(file, Original(), 14400));
    const long size = ftell(file); CHECK(size > 72);
    rewind(file);
    std::vector<unsigned char> bytes(size);
    CHECK(fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    CHECK(fclose(file) == 0);
    return bytes;
}
static bool Decode(const std::vector<unsigned char>& bytes, RecordingSlot& slot) {
    FILE* file = tmpfile(); CHECK(file);
    CHECK(bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
    rewind(file);
    const bool result = RecordingIO::Read(file, slot, 14400);
    CHECK(fclose(file) == 0);
    return result;
}
static void RecordingTests() {
    const auto good = Encode();
    auto slot = Original();
    CHECK(Decode(good, slot)); CHECK(Unchanged(slot));
    // Every possible truncated prefix must fail without touching an occupied slot.
    for (size_t n = 0; n < good.size(); ++n) {
        slot = Original();
        CHECK(!Decode(std::vector<unsigned char>(good.begin(), good.begin() + n), slot));
        CHECK(Unchanged(slot));
    }
    auto bad = good; bad[0] = 0;
    CHECK(!Decode(bad, slot)); CHECK(Unchanged(slot));
    bad = good; const uint32_t tooMany = 14401;
    memcpy(bad.data() + 68, &tooMany, 4);
    CHECK(!Decode(bad, slot)); CHECK(Unchanged(slot));
    auto legacy = good;
    legacy[3] = 0x31;
    legacy.erase(legacy.begin() + 36, legacy.begin() + 68);
    CHECK(Decode(legacy, slot)); CHECK(slot.characterName[0] == '\0');
    CHECK(slot.id == 5 && slot.totalFrames == 2 && slot.frames[1].btnMask == 456);
    bad = good; const uint32_t zero = 0;
    memcpy(bad.data() + 68, &zero, 4); bad.resize(72);
    CHECK(Decode(bad, slot)); CHECK(slot.isEmpty && slot.totalFrames == 0 && slot.frames.empty());

    CreateDirectoryA("tests\\artifacts", nullptr);
    const char* path = "tests\\artifacts\\recording.mkr";
    slot = Original();
    CHECK(RecordingIO::Export(path, slot, 14400));
    FILE* file = nullptr; CHECK(fopen_s(&file, path, "rb") == 0);
    RecordingSlot restored; restored.id = 5;
    CHECK(RecordingIO::Read(file, restored, 14400)); CHECK(Unchanged(restored));
    CHECK(!RecordingIO::Write(file, slot, 14400)); // A stream opened read-only must fail.
    CHECK(fclose(file) == 0);
    slot.totalFrames = 3; // Inconsistent metadata must never read past the vector.
    CHECK(!RecordingIO::Export(path, slot, 14400));
    HANDLE lock = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    slot = Original(); strcpy_s(slot.name, "Must not replace locked file");
    CHECK(!RecordingIO::Export(path, slot, 14400));
    CloseHandle(lock);
    CHECK(fopen_s(&file, path, "rb") == 0);
    CHECK(RecordingIO::Read(file, restored, 14400)); CHECK(Unchanged(restored));
    CHECK(fclose(file) == 0);
}

static void HookTests() {
    void* original[2] = {};
    const HookSpec hooks[] = {{(void*)1, (void*)3, &original[0]}, {(void*)2, (void*)4, &original[1]}};
    // Success, then failure at each of the two creation and two enable stages.
    for (int failure = -1; failure < 4; ++failure) {
        int operation = 0, errors = 0;
        bool created[2] = {}, enabled[2] = {};
        auto index = [](void* address) { return static_cast<int>(reinterpret_cast<uintptr_t>(address)) - 1; };
        const bool result = InstallHookPair(hooks,
            [&](void* target, void*, void**) {
                if (operation++ == failure) return MH_ERROR_UNSUPPORTED_FUNCTION;
                created[index(target)] = true; return MH_OK;
            },
            [&](void* target) {
                if (operation++ == failure) return MH_ERROR_MEMORY_PROTECT;
                enabled[index(target)] = true; return MH_OK;
            },
            [&](void* target) { enabled[index(target)] = false; return MH_OK; },
            [&](void* target) { created[index(target)] = false; return MH_OK; },
            [&](const char*, void*, MH_STATUS) { ++errors; });
        CHECK(result == (failure == -1));
        if (failure == -1) {
            CHECK(created[0] && created[1] && enabled[0] && enabled[1]); CHECK(errors == 0);
        } else {
            CHECK(!created[0] && !created[1] && !enabled[0] && !enabled[1]); CHECK(errors == 1);
        }
    }
}

// Map the on-disk executable into an ordinary private byte buffer. Never load or execute it.
static void CompatibilityTests(const char* executable) {
    FILE* file = nullptr; CHECK(fopen_s(&file, executable, "rb") == 0);
    CHECK(fseek(file, 0, SEEK_END) == 0); const long length = ftell(file); rewind(file);
    std::vector<unsigned char> disk(length); CHECK(fread(disk.data(), 1, disk.size(), file) == disk.size()); fclose(file);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(disk.data());
    const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(disk.data() + dos->e_lfanew);
    std::vector<unsigned char> image(pe->OptionalHeader.SizeOfImage);
    memcpy(image.data(), disk.data(), pe->OptionalHeader.SizeOfHeaders);
    const auto sections = IMAGE_FIRST_SECTION(pe);
    for (unsigned i = 0; i < pe->FileHeader.NumberOfSections; ++i)
        memcpy(image.data() + sections[i].VirtualAddress, disk.data() + sections[i].PointerToRawData, sections[i].SizeOfRawData);
    const auto base = reinterpret_cast<uintptr_t>(image.data());
    CHECK(ValidateExecutableImage(base));
    CHECK(!ValidateExecutableImage(base, true));
    image[0x8a5490 - 0x400000] ^= 1;
    CHECK(!ValidateExecutableImage(base));
    image[0x8a5490 - 0x400000] ^= 1;
    memset(image.data() + 0x5691eb - 0x400000, 0x90, 6);
    memset(image.data() + 0x5692f7 - 0x400000, 0x90, 2);
    const unsigned char patch[] = {0xe9, 0xc7, 0, 0, 0, 0x90};
    memcpy(image.data() + 0x58c4f5 - 0x400000, patch, sizeof(patch));
    CHECK(ValidateExecutableImage(base, true));
    CHECK(!ValidateExecutableImage(base));
    auto mappedPe = reinterpret_cast<IMAGE_NT_HEADERS32*>(image.data() + dos->e_lfanew);
    mappedPe->FileHeader.TimeDateStamp ^= 1;
    CHECK(!ValidateExecutableImage(base, true));
}
static void ProfilerTests() {
    CHECK(!ModProfiler::IsEnabled());
    ModProfiler::SetEnabled(true);
    ModProfiler::Init();
    CHECK(ModProfiler::GetFrequency() > 0);
    ModProfiler::ResetPeaks();
    std::atomic<bool> done{false};
    std::thread observer([&]() {
        while (!done.load()) {
            const auto metrics = ModProfiler::GetMetrics();
            if (metrics[PROF_TRADE_ENGINE].callCount > 40000 ||
                metrics[PROF_TRADE_ENGINE].peakUs > 125.0) std::abort();
            ModProfiler::GetTotalAvgUs();
            ModProfiler::GetTotalPeakUs();
        }
    });
    std::vector<std::thread> writers;
    for (int i = 0; i < 4; ++i) writers.emplace_back([]() {
        for (int n = 0; n < 10000; ++n) ModProfiler::RecordSample(PROF_TRADE_ENGINE, 125.0);
    });
    for (auto& writer : writers) writer.join();
    done = true; observer.join();
    auto metrics = ModProfiler::GetMetrics();
    CHECK(metrics[PROF_TRADE_ENGINE].callCount == 40000);
    CHECK(metrics[PROF_TRADE_ENGINE].peakUs == 125.0);
    CHECK(metrics[PROF_TRADE_ENGINE].avgUs == 125.0);
    metrics[PROF_TRADE_ENGINE].peakUs = 0;
    CHECK(ModProfiler::GetMetrics()[PROF_TRADE_ENGINE].peakUs == 125.0);
    ModProfiler::RecordSample(PROF_TRADE_ENGINE, -1);
    ModProfiler::RecordSample(PROF_TRADE_ENGINE, std::numeric_limits<double>::quiet_NaN());
    CHECK(ModProfiler::GetMetrics()[PROF_TRADE_ENGINE].callCount == 40000);
    ModProfiler::SetEnabled(false);
    ModProfiler::RecordSample(PROF_TRADE_ENGINE, 999.0);
    CHECK(ModProfiler::StartTimer() == 0);
    CHECK(ModProfiler::GetMetrics()[PROF_TRADE_ENGINE].callCount == 40000);
    ModProfiler::SetEnabled(true);
    ModProfiler::ResetPeaks();
    metrics = ModProfiler::GetMetrics();
    CHECK(metrics[PROF_TRADE_ENGINE].avgUs == 0 && metrics[PROF_TRADE_ENGINE].peakUs == 0 && metrics[PROF_TRADE_ENGINE].callCount == 0);
}
static void AccountingTests() {
    using namespace MK9TradeFix::LoadProfiler;
    std::vector<PackageRecord> startup, match;
    match.push_back({L"same.xxx", 1000, 0, 0});
    match.push_back({L"same.xxx", 1000, 0, 0});
    ActiveFileHandle handle = {};
    handle.packageIndex = 1; handle.epoch = 7;
    AttributeRead(handle, 40, 0.25, 7, startup, match);
    CHECK(match[0].bytesRead == 0 && match[1].bytesRead == 40);
    CHECK(match[1].readDurationSec == 0.25 && handle.bytesRead == 40);
    AttributeRead(handle, 10, 0.5, 8, startup, match);
    CHECK(match[1].bytesRead == 40 && handle.bytesRead == 50);
    startup.push_back({L"startup.xxx", 900, 0, 0});
    handle.startup = true; handle.packageIndex = 0;
    AttributeRead(handle, 90, 0.125, 8, startup, match);
    CHECK(startup[0].bytesRead == 90 && startup[0].readDurationSec == 0.125);
    handle.packageIndex = static_cast<size_t>(-1);
    AttributeRead(handle, 5, 0.1, 8, startup, match);
    CHECK(startup[0].bytesRead == 90);
}
int main(int argc, char** argv) {
    CHECK(IsToastyCleanupWait("p_toasty_display",209,200));
    CHECK(!IsToastyCleanupWait("p_toasty_display",181,20));
    CHECK(!IsToastyCleanupWait("p_toasty_display",195,15));
    CHECK(!IsToastyCleanupWait("p_toasty_display",209,120));
    CHECK(!IsToastyCleanupWait("other_script",209,200));
    CHECK(!IsToastyCleanupWait(nullptr,209,200));
    CHECK(CanStartToasty(true,false,false));
    CHECK(CanStartToasty(false,true,false));
    CHECK(!CanStartToasty(false,false,false));
    CHECK(!CanStartToasty(true,false,true));
    CHECK(!CanStartToasty(true,true,true));
    CHECK(!CanStartToasty(false,true,true));
    CHECK(StartupScreenWaitKind("AttractModeLegalScreen","WaitForScreenDisplay",528,60,240)==1);
    CHECK(StartupScreenWaitKind("AttractModeLegalScreen","WaitForScreenDisplay",580,20,180)==2);
    CHECK(StartupScreenWaitKind("AttractModeLegalScreen","WaitForScreenDisplay",532,15,180)==0);
    CHECK(StartupScreenWaitKind("AttractModeLegalScreen","WaitForLoadSaveToComplete",528,60,240)==0);
    CHECK(StartupScreenWaitKind("MainMenu","WaitForScreenDisplay",528,60,240)==0);
    CHECK(StartupScreenWaitKind("AttractModeLegalScreen","WaitForScreenDisplay",529,60,240)==0);
    CHECK(StartupScreenWaitKind("AttractModeLegalScreen","WaitForScreenDisplay",528,15,180)==0);
    CHECK(StartupScreenWaitKind(nullptr,"WaitForScreenDisplay",528,60,240)==0);
    CHECK(StartupScreenWaitKind("AttractModeLegalScreen",nullptr,528,60,240)==0);    CHECK(RegularWakeupInternalDelay(0)==2);
    CHECK(RegularWakeupInternalDelay(1)==2);
    CHECK(RegularWakeupInternalDelay(5)==6);    {
        CHECK(RollWakeupPolicy::IsWakeupMessage("wakeup attack"));
        CHECK(!RollWakeupPolicy::IsWakeupMessage("first hit"));
        CHECK(!RollWakeupPolicy::IsWakeupMessage(nullptr));
        RollWakeupPolicy::Window w;
        w.Observe(1,0x5200,10);
        CHECK(!w.Blocks(true,0,1)); // Ordinary attacks cannot arm a roll restriction.
        CHECK(RollWakeupPolicy::IsRollAnimation("Generic_RollGetup_Back_FeetFacing"));
        CHECK(!RollWakeupPolicy::IsRollAnimation("Generic_Getup_FeetFacing"));
        CHECK(!RollWakeupPolicy::IsRollAnimation("Supermove_Slide_Forward02"));
        CHECK(!RollWakeupPolicy::IsRollAnimation(nullptr));
        w.Arm(1,15); w.Observe(1,0x460D,16);
        CHECK(w.Blocks(true,0,1)); // Captured roll stays in floor action 0x460D.
        w.Observe(1,0x6202,17);
        CHECK(w.Blocks(true,0,1));
        w.Observe(1,0,18);
        CHECK(!w.active);
        w.Arm(1,20); w.Observe(1,0x5200,21);
        CHECK(w.Blocks(true,0,1));
        CHECK(!w.Blocks(false,0,1));
        CHECK(!w.Blocks(true,2,1));
        CHECK(w.Blocks(true,1,1));
        w.Observe(1,0x6200,25);
        CHECK(w.Blocks(true,0,1)); // Rolling getup can transition before returning neutral.
        w.Observe(1,0,30);
        CHECK(!w.Blocks(true,0,1));
        w.Arm(1,40); w.Observe(1,0x4600,42);
        CHECK(!w.active); // Getting hit ends the old roll window.
        w.Arm(1,50); w.Observe(2,0x5200,51);
        CHECK(!w.active); // Fighter replacement cannot inherit restriction.
        w.Arm(1,60); w.Observe(1,0x5200,241);
        CHECK(!w.active);
        w.Arm(1,60); w.Observe(1,0x5200,1);
        CHECK(!w.active); // Reset frame counter.
        CHECK(!w.Blocks(true,0,0));
    }    {
        using namespace HitOutcomePolicy;
        MoveLifecycleTracker t;
        t.inMove=true; t.startFrame=100; t.firstActiveFrame=109;
        t.recoveryStartFrame=114; t.scriptPackage=0x1234; t.scriptFunc=77;
        // A completed jab must not make the later idle recipient a counter or punish.
        for(uint32_t a : {0u,0xFu,0x100u,0x101u,0x102u,0x200u,0x900u,0xA01u,0x2010u,0x6200u})
            CHECK(Classify(a,0x1234,77,t,120)==Outcome::Clean);
        CHECK(Classify(0x5200,0x1234,77,t,105)==Outcome::Counter);
        CHECK(Classify(0x5200,0x1234,77,t,110)==Outcome::Counter);
        CHECK(Classify(0x5200,0x1234,77,t,114)==Outcome::Punish);
        CHECK(Classify(0x5200,0x1234,77,t,120)==Outcome::Punish);
        // A different current attack cannot inherit the old move's recovery phase.
        CHECK(Classify(0x5200,0x1234,78,t,120)==Outcome::Counter);
        CHECK(Classify(0x5200,0x5678,77,t,120)==Outcome::Counter);
        CHECK(Classify(0x5200,0,-1,t,120)==Outcome::Clean);
        CHECK(Classify(0x4600,0x1234,77,t,120)==Outcome::Combo);
        CHECK(Classify(0x4F00,0x1234,77,t,120)==Outcome::Combo);
        t.inMove=false;
        CHECK(Classify(0,0x1234,77,t,120)==Outcome::Clean);
        CHECK(Classify(0x5200,0x1234,77,t,120)==Outcome::Counter);
    }
    CHECK(CleanFighters::BloodTemplate("FX_Blood","Blood_Impact01_InGame_FX"));
    CHECK(CleanFighters::BloodTemplate("FX_Blood","FX_BloodSpatter01_Env_Decal"));
    CHECK(CleanFighters::BloodTemplate("CHAR_Scorpion","FX_Blood_01"));
    CHECK(CleanFighters::BloodTemplate("CHAR_SubZero","Blood_Spew_FX"));
    CHECK(!CleanFighters::BloodTemplate("CHAR_SubZero","SZ_Projectile_Ice"));
    CHECK(!CleanFighters::BloodTemplate("CHAR_Scorpion","SC_Spear"));
    CHECK(!CleanFighters::BloodTemplate("CHAR_SubZero","SZ_IceSword_Blade_Bloody_FX"));
    CHECK(!CleanFighters::BloodTemplate("BGND_Bloodmarsh","Fog"));
    CHECK(!CleanFighters::BloodTemplate("FX_BloodOther","Anything"));
    CHECK(!CleanFighters::BloodTemplate("Transient","ParticleSystem"));
    CHECK(!CleanFighters::BloodTemplate(nullptr,"Blood_Impact"));
    CHECK(CleanFighters::BloodTemplate("FightingArt-04b73c4a","Blood_Impact01_InGame_FX"));
    CHECK(CleanFighters::BloodTemplate("FightingArt-04b73c4a","Blood_Impact01_InGame_Decal_FX"));
    CHECK(!CleanFighters::BloodTemplate("FightingArt-04b73c4a","BodySlam_Dust_FX"));
    CHECK(!CleanFighters::BloodTemplate("FightingArt-04b73c4a","Footfall_Impact_Dust_FX"));
    CHECK(!CleanFighters::BloodTemplate("FightingArt-INVALID!","Blood_Impact01_InGame_FX"));
    CHECK(CleanFighters::BloodTemplate("CHAR_SubZero-8a85da56","Blood_Spew_FX"));
    CHECK(!CleanFighters::BloodTemplate("CHAR_SubZero-8a85da56","SZ_Projectile_Ice"));
    // Replay the reported standing stance-switch -> normal attack timing.
    for(uint64_t gap : {6ull,9ull,20ull,45ull}) {
        uint64_t lastKnockdown=0;
        if(IsFrameDataGetupAction(0x6212)) lastKnockdown=100;
        CHECK(!IsFrameDataWakeupTransition(0,lastKnockdown,100+gap));
        CHECK(!IsFrameDataWakeupTransition(0x6212,lastKnockdown,100+gap));
    }
    CHECK(IsFrameDataWakeupTransition(0x6000,0,100));
    CHECK(IsFrameDataWakeupTransition(0x6100,0,100));
    CHECK(IsFrameDataWakeupTransition(0x620A,0,100));
    CHECK(IsFrameDataWakeupTransition(0x620B,0,100));
    CHECK(IsFrameDataWakeupTransition(0x460D,0,100));
    CHECK(IsFrameDataWakeupTransition(0,100,120));
    CHECK(!IsFrameDataWakeupTransition(0,100,121));
    CHECK(!IsFrameDataWakeupTransition(0,200,100));
    CHECK(argc == 2);
    RecordingTests();
    HookTests();
    ProfilerTests();
    AccountingTests();
    {
        MoveLifecycleTracker tracker;
        CHECK(StartsAttackSegment(tracker, 99, 123, 30));
        tracker.inMove = true; tracker.scriptPackage = 99; tracker.scriptFunc = 123; tracker.nativeRecord = 30;
        CHECK(!StartsAttackSegment(tracker, 99, 123, 30));
        CHECK(StartsAttackSegment(tracker, 99, 123, 1)); // 11 continuation, including native ring wrap.
        CHECK(!StartsAttackSegment(tracker, 99, 123, 0)); // Missing observation is not a new segment.
        CHECK(StartsAttackSegment(tracker, 100, 123, 30));
        ObserveAttackActivity(tracker, 4, true);
        ObserveAttackActivity(tracker, 4, true); // Tick + hit callback must not double-count.
        ObserveAttackActivity(tracker, 5, true);
        CHECK(ObservedActiveFrames(tracker) == 2);
        ObserveAttackActivity(tracker, 6, false);
        CHECK(tracker.recoveryStartFrame == 6);
        ObserveAttackActivity(tracker, 8, true);
        CHECK(tracker.recoveryStartFrame == 0 && tracker.activeBurstStartFrame == 8);
        CHECK(ObservedActiveFrames(tracker) == 3); // Excludes the gap between active bursts.
        ObserveAttackActivity(tracker, 9, false);
        CHECK(tracker.recoveryStartFrame == 9);
        CHECK(!IsDetachedProjectileRecord(14)); // F+2 record in the live reproduction.
        CHECK(IsDetachedProjectileRecord(19));
        tracker = MoveLifecycleTracker(); tracker.inMove = true;
        FinalizeAttackActivity(tracker, 1); // Prepared geometry alone is not activity.
        CHECK(tracker.firstActiveFrame == 0 && tracker.recoveryStartFrame == 0);
        ObserveAttackActivity(tracker, 6, true);
        FinalizeAttackActivity(tracker, 6);
        CHECK(tracker.recoveryStartFrame == 0 && ObservedActiveFrames(tracker) == 1);
        ObserveAttackActivity(tracker, 7, true);
        FinalizeAttackActivity(tracker, 7);
        CHECK(tracker.activeBurstStartFrame == 6 && ObservedActiveFrames(tracker) == 2);
        FinalizeAttackActivity(tracker, 8);
        CHECK(tracker.recoveryStartFrame == 8);
    }
    {
        using namespace MK9TradeFix::JuggleDeterioration;
        CHECK(TargetsPlayer(0,0) && TargetsPlayer(0,1));
        CHECK(TargetsPlayer(1,0) && !TargetsPlayer(1,1));
        CHECK(TargetsPlayer(2,1) && !TargetsPlayer(2,0));
        CHECK(!TargetsPlayer(0,-1));
        CHECK(!IsJuggleReaction(4) && !IsJuggleReaction(0x5200));
        CHECK(IsJuggleReaction(0x4600) && !IsJuggleReaction(0x4205));
        CHECK(EffectiveMotionScale(false, 2, false, .08f, 8) == 1);
        CHECK(EffectiveMotionScale(true, 1.5f, true, .1f, 0) == 1.5f);
        CHECK(std::fabs(EffectiveMotionScale(true, 1.5f, true, .1f, 2)-1.3f)<.0001f);
        CHECK(EffectiveMotionScale(false, 1, true, .3f, 100)==.25f);
        const BallisticParameters native={-1000,500,0};
        LaunchGravityState state;
        const auto standard=native;
        CHECK(HeightAt(standard,.5f)==125);
        const BallisticParameters floaty={state.Begin(-1000,1.5f),500,0}; state.Finish(floaty.gravity,1.5f);
        CHECK(std::fabs(HeightAt(floaty,.75f)-187.5f)<.001f);
        CHECK(std::fabs(HeightAt(floaty,1.5f))<.001f);
        const BallisticParameters heavy={-2000,500,0};
        CHECK(HeightAt(heavy,.25f)==62.5f);
        CHECK(HeightAt(heavy,.5f)==0);
        CHECK(std::fabs(state.Begin(floaty.gravity,1.5f)-floaty.gravity)<.001f); // No compounding on repeated initialization.
        CHECK(state.Begin(floaty.gravity,1)==-1000); // Disable / untargeted normal jump restores source.
        CHECK(state.Begin(-800,2)==-400); // A newly authored launch takes precedence.
    }
    {
        using namespace MK9TradeFix::StageVisibility;
        CHECK(FighterAsset("CHAR_SubZero-8a85da56"));
        CHECK(FighterAsset("char_scorpion"));
        CHECK(StageFXAsset("FX_KahnsColiseum","BGND_KahnsColiseum"));
        CHECK(StageFXAsset("FX_Pit","BGND_PitDay"));
        CHECK(!StageFXAsset("FX_Scorpion","BGND_PitDay"));
        CHECK(!StageFXAsset("FX_Common","BGND_KahnsColiseum"));
        CHECK(!StageFXAsset("FX_KahnsColiseum","BGND_ShangThrone"));
        CHECK(Classify(true,false,true,false,true,false)==Kind::Keep);
        CHECK(Classify(false,true,true,false,true,false)==Kind::Keep); // Fighter swords can use stage-level actors.
        CHECK(Classify(false,true,false,true,false,true)==Kind::Keep); // Ice Ball/vapor resources take priority.
        CHECK(Classify(false,false,true,false,true,false)==Kind::Scenery);
        CHECK(Classify(false,false,false,true,false,true)==Kind::AmbientFX);
        CHECK(Classify(false,false,false,true,false,false)==Kind::Keep); // Level ownership alone is insufficient.
        CHECK(Classify(false,false,true,false,false,false)==Kind::Keep);
        CHECK(PlacedStageAmbient(true,true,0x201,false)); // Placed flame mesh in a streamed stage.
        CHECK(PlacedStageAmbient(true,true,0x10,false)); // Nonstatic, nondeletable level fixture.
        CHECK(!PlacedStageAmbient(true,true,0x200,false)); // Dynamic projectile actor.
        CHECK(!PlacedStageAmbient(true,false,0x201,false)); // Attached/template component.
        CHECK(!PlacedStageAmbient(false,true,0x201,false)); // Class defaults/assets.
        CHECK(!PlacedStageAmbient(true,true,0x201,true)); // Fighter cinematic package.
        CHECK(StageSceneActor("InterpActor")); // Soulnado outer layers.
        CHECK(StageSceneActor("SkeletalMeshActor")); // Soulnado center.
        CHECK(StageSceneActor("StaticMeshActor"));
        CHECK(StageSceneActor("Emitter"));
        CHECK(!StageSceneActor("MKParticleActor")); // Native combat-spawn class.
        CHECK(!StageSceneActor("MKPawn"));
        CHECK(StageSceneActor("DynamicSMActor")); // Desert SandCube.
        CHECK(StageSceneActor("EmitterSpawnable")); // Rooftop tracers.
        CHECK(StageMapAsset("BGND_Courtyard-6dcea7eb"));
        CHECK(StageMapAsset("BGND_Hell-76b84b98"));
        CHECK(StageMapAsset("BGND_Pit-feb0898e"));
        CHECK(StageMapAsset("BGND_Desert-2cc84a8c"));
        CHECK(!StageMapAsset("CHAR_SubZero-8a85da56"));
        CHECK(!StageMapAsset("FX_Scorpion"));
        CHECK(!StageMapAsset("FightingArt-04b73c4a"));
        CHECK(StageWorldFXAsset("FX_RoofDay","RoofDay_Global_FX"));
        CHECK(StageWorldFXAsset("FX_Pit","Pit_Cine_Fatality"));
        CHECK(!StageWorldFXAsset("FX_Blood","Pit_Cine_Fatality"));
        CHECK(!StageWorldFXAsset("FX_Scorpion","RoofDay_Global_FX"));
        CHECK(!StageWorldFXAsset("FX_Pit","PitBottom_Global_FX"));
        CHECK(!StageWorldFXAsset("FX_","RoofDay_Global_FX"));
        CHECK(Classify(false,false,true,false,StageMapAsset("BGND_Courtyard-6dcea7eb"),false)==Kind::Scenery);
        CHECK(Classify(false,false,false,false,false,false,StageMapAsset("BGND_Desert-2cc84a8c"))==Kind::AmbientFX);
        CHECK(Classify(true,false,true,false,StageMapAsset("BGND_Courtyard-6dcea7eb"),false)==Kind::Keep);
        CHECK(SameStageCinemaWorld("Waste_Global_Background","Waste_Cine_World"));
        CHECK(SameStageCinemaWorld("Grave_Global_FX","Grave_Cine_World"));
        CHECK(!SameStageCinemaWorld("Waste_Global_Background","Grave_Cine_World"));
        CHECK(!SameStageCinemaWorld("Waste_Global_Background","BGND_Wastelands"));
        CHECK(!SameStageCinemaWorld("Waste_Global_Background","Waste_CineOther"));
        CHECK(!SameStageCinemaWorld("CHAR_SubZero_Global_FX","CHAR_SubZero_Cine_World"));
        CHECK(!SameStageCinemaWorld("_Global_FX","_Cine_World"));
        CHECK(Classify(false,false,false,false,false,false,true)==Kind::AmbientFX); // Translucent static stage mesh.
        CHECK(Classify(false,false,false,true,false,false,true)==Kind::AmbientFX); // Placed stage emitter.
        CHECK(Classify(true,false,false,false,false,false,true)==Kind::Keep);
        CHECK(Classify(false,true,false,false,false,false,true)==Kind::Keep); // Fighter prop/projectile wins.
    }
    {
        using namespace MK9TradeFix::VoidLighting;
        CHECK(!SoulStealLanded(false,"Teleport_Punch","Teleport_Punch_Hit","p_scorpion_teleport","Hit",0x5200,0x4600));
        CHECK(!SoulStealLanded(false,"Soul_Steal","Soul_Steal","p_scorpion_teleport","Hit",0x5200,0x4600));
        CHECK(SoulStealLanded(true,"Soul_Steal","Soul_Steal_Hit","p_shangtsung_soul_steal","Hit",0x5200,0x4600));
        CHECK(SoulStealLanded(true,"SOUL_STEAL_EX","SOUL_STEAL_HIT_EX","p_shangtsung_soul_steal_ex","Hit",0x5300,0x460D));
        CHECK(SoulStealLanded(false,"","","p_shangtsung_soul_steal_hit","Hit",0x5200,0x4600)); // Authored script survives a form swap.
        CHECK(!SoulStealLanded(true,"Soul_Steal","Soul_Steal_Miss","p_shangtsung_soul_steal","Hit",0x5200,0x4600));
        CHECK(!SoulStealLanded(true,"Soul_Steal","Idle","p_shangtsung_attack1","Hit",0x5200,0x4600)); // Stale cached move.
        CHECK(!SoulStealLanded(true,"Soul_Steal","Soul_Steal","p_shangtsung_soul_steal","Idle",0x5200,0x100));
        CHECK(!SoulStealLanded(true,"Soul_Steal","Idle","","Soul_Steal_Victim",0x100,0x4600));
        CHECK(!SoulStealLanded(false,nullptr,nullptr,nullptr,nullptr,0x5200,0x4600));
        CHECK(!FormChanged(0,123,0,456)); // Initial load has its own warmup.
        CHECK(!FormChanged(123,123,456,456)); // Teleport / pose / actor recreation with the same form.
        CHECK(FormChanged(123,789,456,456));
        CHECK(FormChanged(123,123,456,789));
        CHECK(!FormChanged(123,0,456,0)); // Transient missing component is not a new form.
    }
    {
        using namespace MK9TradeFix::VoidRender;
        CHECK(SceneTarget(1600,900,1600,900,false,true));
        CHECK(SceneTarget(1280,720,1280,720,false,true));
        CHECK(!SceneTarget(64,64,1600,900,true,true)); // Lighting cubemap.
        CHECK(!SceneTarget(1024,1024,1600,900,false,true)); // Shadow target.
        CHECK(!SceneTarget(800,450,1600,900,false,true)); // Downsample / bloom.
        CHECK(!SceneTarget(1600,900,1600,900,true,true)); // A large cubemap is still auxiliary.
        CHECK(!SceneTarget(1600,900,1600,900,false,false)); // Non-color data target.
        CHECK(!SceneTarget(0,0,0,0,false,true));
        CHECK(Active(true,true)); // No warmup or Soul Steal bypass.
        CHECK(!Active(false,true));
        CHECK(!Active(true,false));
    }
    {
        StageBounds::Interval bounds{};
        CHECK(StageBounds::Resolve(0.0f, 2769.138f, bounds)); // Street capture.
        CHECK(std::fabs(bounds.center + bounds.halfWidth - 2769.138f) < 0.001f);
        CHECK(std::fabs(bounds.center - bounds.halfWidth) < 0.001f);
        CHECK(StageBounds::Resolve(0.0f, 2600.0f, bounds));
        CHECK(bounds.center == 1300.0f && bounds.halfWidth == 1300.0f);
        CHECK(StageBounds::Resolve(100.0f, 3300.0f, bounds)); // Shifted limits.
        CHECK(bounds.center == 1700.0f && bounds.halfWidth == 1600.0f);
        CHECK(!StageBounds::Resolve(2000.0f, 1000.0f, bounds));
        CHECK(!StageBounds::Resolve(0.0f, std::numeric_limits<float>::quiet_NaN(), bounds));
    }
    CompatibilityTests(argv[1]);
    // An unsupported host must be able to load the forwarding DLL without
    // installing game hooks or pinning itself into this test process.
    HMODULE mod = LoadLibraryA("bin\\dinput8.dll");
    CHECK(mod != nullptr);
    CHECK(GetProcAddress(mod, "DirectInput8Create") != nullptr);
    CHECK(FreeLibrary(mod));
    std::printf("PASS: %d checks (recording round trips, every truncated prefix, legacy/empty/oversized input, export failures, executable compatibility, hook rollback, unsupported-host DLL load)\n", checks);
}













