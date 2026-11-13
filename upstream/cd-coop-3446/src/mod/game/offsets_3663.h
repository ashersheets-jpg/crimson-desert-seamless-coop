#pragma once
// Addresses for CrimsonDesert.exe 1.0.0.3663 (Co-op+ port, 2026-11-13).
//
// Steam updated the game from 1.0.0.3576 (PE 0x6AED2E85) to 1.0.0.3663 (PE 0x6AF5D198, SizeOfImage 0x144E9000,
// 335,440,280 bytes) on 2026-11-13 02:24. A far bigger change than the earlier ones: the code grew by 0x7C000
// (shifts from +0x3650 to +0x7C000), 1066 of the 1175 message numbers changed, structures grew, and the 127
// info-table getters were rewritten. Every value was mapped with both exes on disk: build/port3663/fmap.py pairs
// the two builds' functions by their order and size (the exception tables), tools/native-port/p63.py places each
// address in its paired function (the same instructions, or the same call), data and vtables through the code that
// addresses them and RTTI; docs/research/port-3663.md has what needed a hand.
//
#include "mod/game/offsets_3549.h"

namespace cdmp::game::build_3663 {

// Everything this patch did not move (opcodes, struct offsets, wire layout, heads that match).
using build_3549::RelayedAck;
using build_3549::kActionLayerAdvanceHead;
using build_3549::kActionLayersAdvanceHead;
using build_3549::kActorManager_ControlledActor;
using build_3549::kActor_ComponentTable;
using build_3549::kActor_Key;
using build_3549::kActor_OwnerUser;
using build_3549::kCameraBlockSentinelValue;
using build_3549::kCameraBlock_Forward;
using build_3549::kCameraBlock_Position;
using build_3549::kCameraBlock_Projection;
using build_3549::kCameraBlock_Sentinel;
using build_3549::kCameraBlock_View;
using build_3549::kCamera_Block;
using build_3549::kClientClock;
using build_3549::kClientClockHead;
using build_3549::kClientOnRecvPacketHead;
using build_3549::kClientPostPacketHead;
using build_3549::kComponents_CharacterControl;
using build_3549::kComponents_EquipSlot;
using build_3549::kComponents_Status;
using build_3549::kControl_Inner;
using build_3549::kControl_Layer0;
using build_3549::kControl_MoveController;
using build_3549::kControl_WallData;
using build_3549::kCoopPinId;
using build_3549::kEquipSlot_Serial;
using build_3549::kEquipSlot_Table;
using build_3549::kErrorLoggerHead;
using build_3549::kHideFlagsOfCopies;
using build_3549::kHookArgSizeStackOffset;
using build_3549::kItemInfo_Key;
using build_3549::kLayer_Chart;
using build_3549::kLayer_Playback;
using build_3549::kLoadKindMount;
using build_3549::kLoadPositionFromEnd;
using build_3549::kLoadRotationFromEnd;
using build_3549::kLoadingParkHeight;
using build_3549::kLoadingParkRadius;
using build_3549::kMaxStatEntries;
using build_3549::kMethod_Actor;
using build_3549::kMiniGameHolder_Gauge;
using build_3549::kMiniGameRoot_Holder;
using build_3549::kMinimapIconCompanion;
using build_3549::kMinimapIconOutlinedArrow;
using build_3549::kMinimapIconPlayerArrow;
using build_3549::kMoveController_Method;
using build_3549::kMoveMethodWallUp;
using build_3549::kMovementMethodStepHead;
using build_3549::kPhysics_AxisX;
using build_3549::kPhysics_AxisY;
using build_3549::kPhysics_AxisZ;
using build_3549::kPhysics_Position;
using build_3549::kPinColourCount;
using build_3549::kPinGroupNormal;
using build_3549::kPinShapeCount;
using build_3549::kPointClimbEnterHead;
using build_3549::kRemoteCatchSceneId;
using build_3549::kRideEnterHead;
using build_3549::kServerActor_Key;
using build_3549::kServerDockingWriter;
using build_3549::kServerDockingWriterHead;
using build_3549::kServerOtherEquipWriter;
using build_3549::kServerOtherEquipWriterHead;
using build_3549::kSlotEntry_Count;
using build_3549::kSlotEntry_ItemIndex;
using build_3549::kSlotEntry_ShownIndex;
using build_3549::kSlotEntry_Size;
using build_3549::kSlotEntry_Slot;
using build_3549::kSlotTable_Count;
using build_3549::kSlotTable_Entries;
using build_3549::kStatContainer_Count;
using build_3549::kStatContainer_Entries;
using build_3549::kStatEntry_Current;
using build_3549::kStatEntry_Id;
using build_3549::kStatEntry_Max;
using build_3549::kStatEntry_Size;
using build_3549::kStatId_Health;
using build_3549::kStatId_Stamina;
using build_3549::kStatusFlagsChangedHead;
using build_3549::kStatus_HideFlags;
using build_3549::kStatus_StatContainer;
using build_3549::kTroc_Kind;
using build_3549::kTroc_Opcode;
using build_3549::kVtCamera;
using build_3549::kVtPhysicsObject;
using build_3549::kWorldSystem_ActorManager;

// ---- message numbers: 3663 gave 1066 of the 1175 messages another number (build/port3663/troc_3576_3663.json,
// read from both exes by message name). In brackets: the 3576 number.
inline constexpr uint16_t kOpActorLoadDataAck = 2005;  // (2005)
inline constexpr uint16_t kOpChangeDockingSocketByFrameEventAck = 2155;  // (2907)
inline constexpr uint16_t kOpCreateOrChangePinMarkerAck = 2267;  // (2647)
inline constexpr uint16_t kOpMoveActorAck = 3008;  // (3094)
inline constexpr uint16_t kOpMoveActorReq = 2790;  // (2833)
inline constexpr uint16_t kOpOtherEquipItemAck = 2426;  // (3136)
inline constexpr uint16_t kOpOtherUnequipItemAck = 2565;  // (2651)
inline constexpr uint16_t kOpRemoteCatchResultAck = 2701;  // (2395)
inline constexpr uint16_t kOpRemoveActorAck = 3037;  // (3067)
inline constexpr uint16_t kOpRemovePinMarkerAck = 2579;  // (2376)
inline constexpr uint16_t kOpRotateActorAck = 2664;  // (3078)
inline constexpr uint16_t kOpRotateActorReq = 2296;  // (2852)
inline constexpr uint16_t kOpSetFieldTimeAck = 3145;  // (2952)
inline constexpr uint16_t kOpSetFieldTimeReq = 2150;  // (2227)
inline constexpr uint16_t kOpStartActionAck = 2800;  // (2552)
inline constexpr uint16_t kOpStartActionReq = 2431;  // (2202)
inline constexpr uint16_t kOpUnequipItemAck = 3155;  // (2615)
inline constexpr uint16_t kOpUpdateOnDemandGimmickAck = 2367;  // (2923)
inline constexpr uint16_t kOpUpdateWallUpDataAck = 2939;  // (3024)

// Relayed server messages (layout: build_3424::RelayedAck; offsets_3460.h has the story). The sizes and key
// fields are the 3479 ones: NOT re-measured on 3663 (the native layer does not relay them).
inline constexpr RelayedAck kRelayedAcks[] = {
    {2608, false, 92, {0x54}},  // VaryHPAck: HP; +0x54 = who caused it
    {2108, false, 90, {0x54}},  // DataDefinedRegenerateValueAck: +0x54 = source
    {2914, false, 41, {}},  // VaryDataDefinedStaticStatAck
    {2148, true, 77, {0x18, 0x1C, 0x21}},  // ApplyBuffAck: caster, target, tagged source (0x22 = actor)
    {2899, true, 77, {0x18, 0x1C, 0x21}},  // UnapplyBuffAck
    {2968, false, 42, {}},  // UpdateChangeBuffLevelAck
    {2849, false, 118, {0x28}},  // UpdateElementalAck: (wet, burning...); +0x28 = source
    {2316, true, 19, {0x04}},  // NotifyHandleDeadAck: +0 dead (the sender), +4 killer
    {2863, true, 0, {}},  // NotifyHandleFatalAck
    {3163, true, 0, {}},  // NotifyHandleReviveAck
    {2158, true, 18, {}},  // ReviveAck
    {2357, true, 279, {}},  // CharacterCustomizationAck
    {kOpRemoteCatchResultAck, true, 97, {}, true, {0x0A}},  // RemoteCatchResultAck (see build_3424)
};

// ---- fields that moved inside the game's structures (tools/native-port/fields63.py has the evidence)
inline constexpr uint32_t kControlInner_Physics = 0x2D0;   // 3576: 0x2C0 (the inner object grew by 0x10)
inline constexpr uint32_t kMoveController_WallUp = 0x178;  // 3576: 0x120 (the movement controller: +0x58 from +0x120)

// ---- addresses (3576 -> 3663: tools/native-port/p63.py) ---------------------------
inline constexpr uint32_t kPeTimestamp = 0x6AF5D198;  // 3576:0x6AED2E85
inline constexpr uintptr_t kImageBase = 0x140000000;  // fixed, no ASLR
inline constexpr uintptr_t kImageSize = 0x144E9000;  // 3576:0x13CC4000
inline constexpr uintptr_t kWorldSystemSlot = 0x72451C0;  // 3549:0x71BDEF0, 8/8 code references agree
inline constexpr uintptr_t kCameraSlot = 0x71602A8;  // 3549:0x70DA028, 2/2
inline constexpr uintptr_t kStaticPlayerPos = 0x7253E60;  // 3549:0x71CD0B0, 2/2
inline constexpr uintptr_t kChunkOrigin = 0x7231330;  // 3549:0x71AA150, 8/8
inline constexpr uintptr_t kCutscenePlaying = 0x7160116;  // 3549:0x70D9F14, 2/2
inline constexpr uintptr_t kFieldTimeClient = 0x6F31C78;  // 3549:0x6EAB998, 6/6
inline constexpr uintptr_t kMiniGameRoot = 0x7245218;  // 3549:0x71BDF48, 8/8
inline constexpr uintptr_t kClientClockGlobal = 0x71600AD;  // 3549:0x70D9E3D, 8/8 cmp sites
inline constexpr uintptr_t kClientFrameClockMs = 0x71600E1;  // = flag + 0x34
inline constexpr uintptr_t kClientFrameClockUs = 0x71600E9;  // = flag + 0x3C
inline constexpr uintptr_t kClientClockBias = 0x7245468;  // = kWorldSystemSlot + 0x2A8 (8/8 references agree)
// Co-op+ low latency (2026-11-06): the client's game clock in ms, one u64. Found live with the dev clockscan
// (dev_memscan.h): it runs with the wall clock and stays ~54 ms ahead of the newest MoveActorReq stamp (the game stamps
// its Reqs that much behind it), unlike flag+0x34 above (0.7 s off). game_clock.cpp reads it on this build.
inline constexpr uintptr_t kClientClockMs = 0x72B7DE8;
inline constexpr uintptr_t kMinimapIconRoot = 0x7245558;  // = kWorldSystemSlot + 0x398
inline constexpr uintptr_t kMinimapIconRootAlt = 0x7245560;  // = kWorldSystemSlot + 0x3A0
inline constexpr uintptr_t kStatusFlagsChanged = 0x715810;  // 3549:0x6E99D0, head matches
inline constexpr uintptr_t kVtNormalMovementMethod = 0x59D6970;  // 3549:0x5958C10 (= RTTI); slot 2 = kMovementMethodStep
inline constexpr uintptr_t kMovementMethodStep = 0xBF51B0;  // 3549:0xBBD5F0, head matches
inline constexpr uintptr_t kVtClientActorManager = 0x59C2760;  // 3549:0x5944B38 (= RTTI)
inline constexpr uintptr_t kVtControlledActor = 0x5990BF0;  // 3549:0x59127C8 (= RTTI ClientChildOnlyInGameActor)
inline constexpr uintptr_t kVtCharacterControl = 0x59C1660;  // 3549:0x59438F0 (= RTTI)
inline constexpr uintptr_t kVtStatusComponent = 0x5998B60;  // 3549:0x591A710 (= RTTI)
inline constexpr uintptr_t kVtWallUpMovementMethod = 0x59A2418;  // 3549:0x5923FA8 (= RTTI)
inline constexpr uintptr_t kActionLayerAdvance = 0x21BD690;  // 3549:0x214EC90, head matches
inline constexpr uintptr_t kActionLayersAdvance = 0x23A27C0;  // 3549:0x2331320; one of 227 identical template copies: the 64th in both builds (the whole block moved +0xD50, its callees too), 0x670 gap to kPending kept
inline constexpr uintptr_t kMinimapIconDecide = 0xA1A16D;  // 3549:0x9EA31D
inline constexpr uintptr_t kVtClientEquipSlotComponent = 0x59C90F8;  // 3549:0x594B388 (= RTTI)
// 3663: the item table is { +0x28 count, +0x20 -> entries of 24 bytes, the record pointer at entry + 8 }
// (3576: { +0x08 count, +0x58 -> record pointers }); the getter's load is `mov rcx,[rip+d]` (was `mov rbx`).
inline constexpr uint32_t kItemInfoTable_Count = 0x28;
inline constexpr uint32_t kItemInfoTable_Records = 0x20;
inline constexpr uint32_t kItemInfoTable_Stride = 24, kItemInfoTable_RecordAt = 8;
inline constexpr uint8_t kItemInfoTableLoadReg = 0x0D;  // the ModRM byte of the load (rcx; rbx was 0x1D)
inline constexpr uintptr_t kItemInfoTableSlot = 0x7246370;  // 3549:0x71BEE70, decoded from the load below
inline constexpr uintptr_t kItemInfoTableLoad = 0x3DAF18;  // 3549:0x3DD5D5
inline constexpr uintptr_t kTrocInstancesBegin = 0x6F14E10;  // 3549:0x6E8ED10, same span
inline constexpr uintptr_t kTrocInstancesEnd = 0x6F487B8;  // last + 8
inline constexpr uintptr_t kGameplayInput = 0x7226558;  // 3549:0x719F558, u8
inline constexpr uintptr_t kUiModePush = 0xF5CA77;  // 3549:0xF21B07; its movzx reads kGameplayInput
inline constexpr uintptr_t kErrorLogger = 0x163D960;  // 3549:0x15FC450, head matches
inline constexpr uintptr_t kClientPostPacket = 0x3A9960;  // 3549:0x3A6030, head matches
inline constexpr uintptr_t kClientOnRecvPacket = 0x3A9CF0;  // 3549:0x3A63C0, head matches
inline constexpr uintptr_t kPointClimbEnter = 0x78D520;  // 3549:0x760A10, head matches
inline constexpr uintptr_t kRideOnEnter = 0x77C3A0;  // 3549:0x74FBB0, head matches
inline constexpr uintptr_t kRideOffEnter = 0x77D370;  // 3549:0x750B80, head matches
inline constexpr uint8_t kMinimapIconDecideHead[] = {0x80, 0x7C, 0x24, 0x48, 0x00, 0x75, 0x6F, 0x8B, 0x05, 0x0A, 0x5A, 0x7B, 0x06, 0x41, 0x89, 0x06};  // its mov eax,[rip+d] moved with the data

}  // namespace cdmp::game::build_3663
