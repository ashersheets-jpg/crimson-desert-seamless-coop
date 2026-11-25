#include "mod/session2/shop_probe.h"
#include "mod/session2/ai_handoff.h"

#include <windows.h>
#include <intrin.h>  // _ReturnAddress (the per-player focus slot's stage-end detour)

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <safetyhook.hpp>

#include "common/troc_frame.h"
#include "mod/game/hook_prepare.h"
#include "mod/game/offsets.h"
#include "mod/game/rtti.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/focus_slot_logic.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/observer_seq_copy.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/server.h"
#include "mod/session2/shop_probe_logic.h"
#include "mod/session2/shop_trade_probe_logic.h"
#include "mod/session2/stage_lookup_logic.h"
#include "mod/session2/user2_shops.h"
#include "mod/session2/user2_shops_logic.h"
#include "mod/session2/story_gate.h"
#include "mod/session2/user2_stages.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;
namespace off = game::build_2976;
using shp::Who;

// ---------------------------------------------------------------- the points (build 2976, .rsrc)
//
// Read with capstone on the read-only PE (p8-impl-tenderos-c1.md §2): whole instructions, no branch of the function
// and no rel32 call/jmp of the PE lands strictly inside a patch (a branch to a patch's first byte is fine).
//   E 0x288B7A0  mov rax,rsp ; mov [rax+0x10],rbx                    7 bytes, the entry of 0x288B7A0 (.pdata
//                                                                    0x288B7A0-0x288BE21); rcx = mgr, rdx = field,
//                                                                    r8 = the manager's character
constexpr uintptr_t kE = 0x2B4FDA0;  // 3576 (2976 0x288B7A0), same head
constexpr uint32_t kEPatch = 7;
constexpr uint8_t kEBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10};
//   G 0x288B87A  cmp [r15+0x180],eax                                 7 bytes (the jne rel32 after it is not
//                                                                    patched); a jmp target at its first byte
//                (0x288B876). Context 0x288B867: mov rax,[r13+0xA0] ; test ; je ; mov eax,[rax+0x60] ; jmp ; mov eax,esi
constexpr uintptr_t kG = 0x2B4FE7A, kGCtx = 0x2B4FE66;  // 3576: as user2_stages' gate (4th argument in rsi)
constexpr uint32_t kGPatch = 7;
constexpr uint8_t kGBytes[] = {0x41, 0x39, 0x87, 0x80, 0x01, 0x00, 0x00, 0x0F, 0x85, 0x80, 0x05, 0x00, 0x00};
constexpr uint8_t kGCtxBytes[] = {0x49, 0x8B, 0x85, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0,
                                  0x74, 0x05, 0x8B, 0x40, 0x60, 0xEB, 0x03, 0x41, 0x8B, 0xC4};
//   P 0x288AAEF  nop ; mov rcx,[rbp+0x77]                            5 bytes, right after call 0x287C200
//                Context 0x288AAD6: mov r9,[rdi+0x200] ; lea r8,[rbp-0x51] ; mov rdx,r14 ; mov rcx,r15 ;
//                vzeroupper ; call 0x287C200 (E8 11 17 FF FF)
// 3576: in the element 0x2AE8700, after call 0x2AD8E60: nop ; mov r12,[rbp-0x69]. There rcx = r14 (the manager), rdx =
// r15 (the result), the element in RBX (2976: r15 / r14 / rdi); the frame is unchanged (playData rbp-0x51, inst rbp+0x77).
constexpr uintptr_t kP = 0x2B4EFB5, kPCtx = 0x2B4EF9C;
constexpr uint32_t kPPatch = 5;
constexpr uint8_t kPBytes[] = {0x90, 0x4C, 0x8B, 0x65, 0x87};
constexpr uint8_t kPCtxBytes[] = {0x4C, 0x8B, 0x8B, 0x00, 0x02, 0x00, 0x00, 0x4C, 0x8D, 0x45, 0xAF, 0x49, 0x8B,
                                  0xD7, 0x49, 0x8B, 0xCE, 0xC5, 0xF8, 0x77, 0xE8, 0xBB, 0x02, 0xFF, 0xFF};
//   C 0x29E9282  mov ebx,[rbp-0x58] ; test ebx,ebx                   5 bytes (the je rel32 after it is not patched)
//                Context 0x29E9276: mov rcx,[rcx+0x98] ; call 0x2B36860 (E8 DE D5 14 00)
// 3576: TrocTrInteractionPreemptionStepReq (vtable 0x5EEC638) slot 2 0x2C70910: mov ebx,[rbp-0x80] after call 0x2DD0720.
constexpr uintptr_t kC = 0x2CDC149, kCCtx = 0x2CDC13D;
constexpr uint32_t kCPatch = 5;
constexpr uint8_t kCBytes[] = {0x8B, 0x5D, 0x80, 0x85, 0xDB};
constexpr uint8_t kCCtxBytes[] = {0x48, 0x8B, 0x89, 0x98, 0x00, 0x00, 0x00, 0xE8, 0x37, 0x00, 0x16, 0x00};
// C0 (2026-11-25, BUG 2): the context's first instruction, mov rcx,[rcx+0x98] (7 bytes), right before the 2642 handler's
// call of its path check 0x2E3C180 (the refusal 0x58C8345D comes from inside that call: cs125a). r13 = the requester's
// actor (C's user key comes from it: the friend's own character for its talks), the frame as at C. With the per-player
// slot on, user 2's talk runs that check with its own focus triple in the host's manager; C closes the window. Installed
// after C (C's context check reads these bytes first).
constexpr uintptr_t kC0 = 0x2CDC13D;
constexpr uint32_t kC0Patch = 7;
constexpr uint8_t kC0Bytes[] = {0x48, 0x8B, 0x89, 0x98, 0x00, 0x00, 0x00};
// B (Co-op+ 2026-11-21, the taming freeze: user 2's StageBranchStateReq "then 2" of its taming scene got no Ack and no
// Nak). 3732: the StageBranchStateReq handler (its vf48 at 0x2D55B36) calls the branch 0x2B56870 (protected) as
// (manager = vf48's rax, &result [rbp-0x80], field [rbp-0x38], seq [rbp-0x80], then [rbp-0x48], [rbp+0x88] u8,
// [rbp+0x80] u8); a result != 0 is answered with a Nak 1013 (0x3F5) at 0x2D55B75.
//   B1 0x2D55B56  mov r9,[rbp-0x80] ; mov r8,[rbp-0x38]   8 bytes: the sequencer, the branch, the manager (rax)
//   B2 0x2D55B6A  mov ebx,[rbp-0x80] ; test ebx,ebx        5 bytes: the result (the je rel32 after it is not patched)
//   Context 0x2D55B36: call [rax+0x48] ; movzx ecx,byte [rbp+0x80]; 0x2D55B5E: lea rdx,[rbp-0x80] ; mov rcx,rax ;
//   call 0x2B56870 (E8 06 0D E0 FF).
constexpr uintptr_t kB1 = 0x2D55B56, kB2 = 0x2D55B6A, kBCtx = 0x2D55B36, kBCallCtx = 0x2D55B5E;
constexpr uint32_t kB1Patch = 8, kB2Patch = 5;
constexpr uint8_t kB1Bytes[] = {0x4C, 0x8B, 0x4D, 0x80, 0x4C, 0x8B, 0x45, 0xC8};
constexpr uint8_t kB2Bytes[] = {0x8B, 0x5D, 0x80, 0x85, 0xDB};
constexpr uint8_t kBCtxBytes[] = {0xFF, 0x50, 0x48, 0x0F, 0xB6, 0x8D, 0x80, 0x00, 0x00, 0x00};
constexpr uint8_t kBCallCtxBytes[] = {0x48, 0x8D, 0x55, 0x80, 0x48, 0x8B, 0xC8, 0xE8, 0x06, 0x0D, 0xE0, 0xFF};
constexpr int32_t kB_Seq = -0x80, kB_Then = -0x48, kB_Field = -0x38, kB_U8a = 0x88, kB_U8b = 0x80, kB_Result = -0x80;
// Co-op+ 2026-11-24 (docs/research/taming-refusal-3732.md): the branch step 0x2C0B8B0's two loads of error global
// 0x71BB2E8, hooked right after the load (eax = the code). BF1 after 2C0BFD7 (a node entry of kind 2 and the element's
// [+0x88]+0x75 set), BF2 after 2C0C03A (the condition call 0x25C82A0 returned false). The frame: rbp as in the step;
// [rbp+0xCE0] = the scene (its first argument's spill), [rbp] = the target node, [rbp-0x70] = the element (BF1).
constexpr uintptr_t kBF1 = 0x2C0BFDD, kBF2 = 0x2C0C040, kBF1Ctx = 0x2C0BFD7, kBF2Ctx = 0x2C0C03A;
constexpr uint32_t kBFPatch = 7;
constexpr uint8_t kBFBytes[] = {0x4C, 0x8B, 0xBD, 0xE8, 0x0C, 0x00, 0x00};  // mov r15,[rbp+0xCE8]
constexpr uint8_t kBF1CtxBytes[] = {0x8B, 0x05, 0x0B, 0xF3, 0x5A, 0x04};     // mov eax,[rip+0x45AF30B] (0x71BB2E8)
constexpr uint8_t kBF2CtxBytes[] = {0x8B, 0x05, 0xA8, 0xF2, 0x5A, 0x04};     // mov eax,[rip+0x45AF2A8] (0x71BB2E8)
constexpr int32_t kBF_Scene = 0xCE0, kBF_Node = 0, kBF_Elem = -0x70;
constexpr uint32_t kBF_SceneOwner = 0x28, kBF_SceneWord = 0x30, kBF_ElemInfo = 0x88, kBF_ElemFlag = 0x75;
constexpr uintptr_t kImageSize3732 = 0x13C7E000;
// Co-op+ 2026-11-24 (late; scratchpad cond73 / cond73b, shp::exclusive_verdict): [scene+0x28] is the scene's MANAGER
// (kStage_Mgr elsewhere; the "owner" the BF line names is that manager's +0x60), and the condition call's first path is
// the manager's exclusive check. "Why" reads, after either refusal: the target node's entry in the scene's processor
// list ([[scene+0x58]+0x10], u32 count +0x18, matched by u32 [entry] == [node+0x10]) and its element flag; the
// manager's current stage (stp::current_of) and whether it is live there (isl::find_stage), with both stages' names
// ([[stage+0x48]+0x1B8]); the node's condition object [[[node]+0x38]] (asked when the exclusive path passes: vtable +8
// or +0x10 with a pa::CompareSequencerTargetParameter), named from its RTTI on the log thread. Read only.
constexpr uint32_t kBF_SceneProc = 0x58, kProc_Entries = 0x10, kProc_Count = 0x18, kNode_EntryId = 0x10;
constexpr uint32_t kScene_Type = 0x68, kScene_Group = 0x78, kScene_Info = 0x48, kInfo_Name = 0x1B8, kNodeData_Cond = 0x38;
constexpr uint32_t kMaxProcEntries = 256;
// Co-op+ 2026-11-24 (late; scratchpad focus73*.py, the research note's "The focus slot's code on 3732"): who writes a
// ServerSequencerStageManager's focus slot (+0x90 / +0x98 / +0xA0) and from where, read only:
//   FS 0x2271252  mov [rcx+0xA0],rdx      the setter, a branch into a flagged node: rcx the manager, rdx the new id,
//                                         [r14+0x10] the scene (its id at +0x30 = rdx)
//   FU 0x21CAF10  push rbx ; sub rsp,0x20 the undo (rcx the manager; [rsp] the return: 0x21CBA3B the delete's clear,
//                                         0x227148C the setter leaving a flagged node)
//   FD 0x21CB970  mov [rsp+0x18],rbx      the clear at a stage's end (rcx the manager, rdx the stage)
//   FR 0x2A10B89 / 0x2A10B9A              around ServerRunThread's one call of ServerNet::OnRecvPacket (its session
//                                         r8 = [r15+0x18]): a thread-local "inside a request of user 2's session"
// None of their functions branches into the patched bytes (focus73c.py).
constexpr uintptr_t kFS = 0x2271252, kFU = 0x21CAF10, kFD = 0x21CB970, kFR1 = 0x2A10B89, kFR2 = 0x2A10B9A;
constexpr uint8_t kFSBytes[] = {0x48, 0x89, 0x91, 0xA0, 0x00, 0x00, 0x00};
constexpr uint8_t kFUBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20};
constexpr uint8_t kFDBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x18};
constexpr uint8_t kFR1Bytes[] = {0x4D, 0x8B, 0x47, 0x18, 0x48, 0x8D, 0x54, 0x24, 0x30};
constexpr uint8_t kFR2Bytes[] = {0x48, 0x8D, 0x94, 0x24, 0xA0, 0x00, 0x00, 0x00};
constexpr uint32_t kFSPatch = 7, kFUPatch = 6, kFDPatch = 5, kFR1Patch = 9, kFR2Patch = 8;
// A stage's owner: cs121a read [[stage+0x48]+0x60] as 0 on every stage of both the host's client and server managers,
// so the u32 at +0x98 (the client stages' owner key, kStage_Owner in guest_seq_foreign_logic.h) is read too; the
// [focus] lines log both until one is seen right on a server stage (the host's own talk, user 2's own shop).
constexpr uint32_t kFS_SceneHolder = 0x10, kStageData_Owner = 0x60, kStage_Owner98 = 0x98;
constexpr uint32_t kMgr_Focus90 = 0x90, kMgr_Focus98 = 0x98, kMgr_FocusA0 = 0xA0;

// Offsets (server objects; the same ones observer_stage / observer_milestone read).
constexpr uint32_t kActor_Key = 0x60, kActor_User = 0xA0, kUser_Key = 0x60, kField_Owner = 0x180, kMgr_Actor = 0x18;
// User 2's character ([user+0xD0], as observer_milestone reads it) and its manager [[[char+0x68]+0xA8]+0x18] (the
// chain ServerField::vf48 0x2B00CE0 follows for the field owner). +0x6A0: the manager's state byte 0x288B7A0 tests.
constexpr uint32_t kUser_Main = 0xD0, kActor_Comps = 0x68, kComps_Seq = 0xA8, kSeqComp_Mgr = 0x18, kMgr_State = 0x6D8;  // 3663 (3576 +0x6D0)
// playData as 0x288A970 builds it: 0x48 bytes from [rbp-0x51] (owner +0, id +8, type +0x10, ...) are logged raw.
constexpr uint32_t kPlayDataLogged = 0x40;
// 0x288A970's frame (rbp = rsp - 0x2F after its pushes): playData at [rbp-0x51] (owner u32 +0, id u64 +8), the
// instance out [rbp+0x77]; the element rdi: +8 the resource (-> -> char* path), +0x200 the binding data.
constexpr int32_t kP_PlayData = -0x51, kP_Id = -0x49, kP_Inst = 0x77;
constexpr uint32_t kElem_Res = 0x08, kElem_Bind = 0x200;
// The 2671 handler's frame: requester u32 [rbp+0x70], NPC u32 [rbp+0x78], interaction u32 [rbp-0x60] (the copy
// passed as r9), result u32 [rbp-0x58]; r13 = the requester's actor (its lock handle is held over the call).
constexpr int32_t kC_Requester = 0x80, kC_Npc = 0x88, kC_Interaction = -0x48, kC_Result = -0x80;  // 3576

uintptr_t g_base = 0;
std::atomic<bool> g_on{false}, g_started{false};
// S2 (p8-impl-tenderos-s2.md): funcnpc_twin (probe only) and what the module's thread knows of user 2: its character
// object and key, and that character's ServerSequencerStageManager (vtable checked), refreshed every second.
std::atomic<uint8_t> g_twin{0};
std::atomic<bool> g_twin_asked_on{false};
std::atomic<uintptr_t> g_u2_char{0}, g_u2_mgr{0};
std::atomic<uint32_t> g_u2_key{0};
shp::IdRing<64> g_twinned;
std::atomic<uint64_t> g_twin_verdicts[static_cast<size_t>(shp::TwinVerdict::Count)] = {};
std::atomic<bool> g_e_on{false}, g_g_on{false}, g_p_on{false}, g_c_on{false}, g_sa_on{false}, g_sr_on{false},
    g_sf_on{false};
SafetyHookMid g_e_hook, g_g_hook, g_p_hook, g_c_hook, g_sa_hook, g_sr_hook, g_sf_hook;
std::atomic<bool> g_b1_on{false}, g_b2_on{false};
SafetyHookMid g_b1_hook, g_b2_hook;
// C0 (BUG 2, the talk window): the hook, and on the logic thread the window it opened (closed at C).
std::atomic<bool> g_c0_on{false};
SafetyHookMid g_c0_hook;
thread_local uintptr_t t_c_mgr = 0;
thread_local uint32_t t_c_token = pfs::FocusSlots::kRefused;
// The host's last accepted talk (C): its NPC and when; with the host's focus set, a talk of user 2's to that same NPC
// is left to the game (its one-talker rule).
std::atomic<uint32_t> g_host_talk_npc{0};
std::atomic<uint64_t> g_slot_talks{0}, g_slot_talks_left{0};
std::atomic<bool> g_bf1_on{false}, g_bf2_on{false};
SafetyHookMid g_bf1_hook, g_bf2_hook;
std::atomic<uint64_t> g_bf[2][2] = {};  // [site][user 2?]
// B: user 2's StageBranchStateReq sequencers (the tap), so the handler's result is told as user 2's.
shp::IdRing<32> g_u2_branch_seqs;
// User 2's own scenes (their sequencer ids from the PrepareSequencerAcks to user 2 naming its character as the owner):
// lock-free, read by the per-player focus slot's detours and the [focus] lines.
shp::IdRing<128> g_u2_own;
std::atomic<uint64_t> g_branch[2] = {}, g_branch_fail[2] = {}, g_branch_unpaired{0};  // [0] user 2, [1] other
struct BranchCall {
    uint64_t seq = 0;
    uintptr_t mgr = 0, field = 0;
    uint32_t then = 0;
    uint8_t a = 0, b = 0;
    bool set = false;
};
thread_local BranchCall t_branch;

shp::KeyTable<16> g_keys;
shp::IdRing<64> g_funcnpc;
isl::IdBook<256> g_book;

std::atomic<uint64_t> g_prep{0}, g_prep_funcnpc{0}, g_prep_fail{0}, g_prep_funcnpc_not_owner{0};
std::atomic<uint64_t> g_2671[3] = {}, g_2671_fail[3] = {};  // by Who
std::atomic<uint64_t> g_srv[3] = {}, g_srv_found[3] = {}, g_srv_result[3][4] = {};
std::atomic<uint64_t> g_req[4] = {}, g_req_filtered[4] = {}, g_req_funcnpc[4] = {};  // 2671 2720 2436 2591
std::atomic<uint64_t> g_out[6] = {}, g_out_funcnpc[6] = {};  // 2663 3000 2916 2525 2981 2524

enum Kind : uint16_t {
    kEGate, kEPrep, kE2671, kESrv, kEReq, kEOut, kETwin, kEBranch, kEBranchFail, kEBranchWhy, kEBranchNames, kEFocus,
    kEOwnScene, kKindCount
};
p::EventRing<512> g_ring;
p::WindowLimiter g_lim[kKindCount] = {{16, 5'000'000}, {48, 5'000'000}, {32, 5'000'000}, {32, 5'000'000},
                                       {48, 5'000'000}, {48, 5'000'000}, {32, 5'000'000}, {32, 5'000'000},
                                       {16, 5'000'000}, {16, 5'000'000}, {16, 5'000'000}, {24, 5'000'000},
                                       {16, 5'000'000}};
// kEOwnScene (2026-11-25): where each scene of user 2's own was at its PrepareSequencerAck: [0] the host's manager
// only, [1] user 2's own manager only, [2] both, [3] neither.
std::atomic<uint64_t> g_own_where[4] = {};
// The focus probe (kFS ...): hooks, counts [set / undo / clear][inside a request of user 2's session], the window.
std::atomic<bool> g_fs_on{false}, g_fu_on{false}, g_fd_on{false}, g_fr1_on{false}, g_fr2_on{false};
SafetyHookMid g_fs_hook, g_fu_hook, g_fd_hook, g_fr1_hook, g_fr2_hook;
std::atomic<uint64_t> g_focus_n[3][2] = {};
std::atomic<uint64_t> g_recv_u2{0}, g_recv_all{0};
thread_local uint32_t t_recv_u2 = 0, t_recv_any = 0;
// The per-player focus slot (user2_focus_slot=1; docs/research/taming-refusal-3732.md, "Per-player focus slot"): the
// joined player's own triple of the HOST manager's focus slot is kept apart (pfs::FocusSlots) and put in the manager
// around user 2's branch / chart-event requests about a scene of its own (FR1 / FR2) and, by their stage's owner (a
// scene of user 2's own, its id in g_u2_own: user 2's triple, else the host's), around the focus setter 0x2271150
// (proc, old, new), a stage processor's tick 0x226DA80 (proc, u8) and a stage's end 0x21CB970 (mgr, stage, u8): inline
// hooks at their entries;
// each takes 3 or fewer register arguments and reads no stack argument (checked in the exe). Only the host's manager
// (refreshed every second from the host's character); the server's thread only.
constexpr uintptr_t kFSetFn = 0x2271150, kFTickFn = 0x226DA80, kFEndFn = kFD;
constexpr uint8_t kFSetHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x4C, 0x89, 0x44, 0x24, 0x18};
constexpr uint8_t kFTickHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54};
constexpr uint8_t kFEndHead[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20};
constexpr uint32_t kProc_Mgr8 = 0x08, kProc_Stage10 = 0x10;
std::atomic<bool> g_slot_wanted{false}, g_slot_on{false};
std::atomic<uintptr_t> g_host_mgr{0};
std::atomic<bool> g_slot_reset_all{false}, g_slot_reset_u2{false};
std::atomic<uint64_t> g_slot_resets{0};  // the slot reset for another host manager (the summary)
SafetyHookInline g_fset_hook, g_ftick_hook, g_fend_hook;
pfs::FocusSlots g_slots;                    // the server's thread only
std::atomic<uint64_t> g_slot_u2[4] = {};    // user 2's pieces of work: requests, setter, stage ends, ticks
std::atomic<uint64_t> g_slot_u2_passed{0};  // user 2's other requests: run with the host's triple, as without the fix
std::atomic<uint64_t> g_slot_other{0};      // other managers' calls passed straight on
// 2026-11-25 redo (cs125d): the server's LOGIC thread, where the stage manager's focus is written and the requests'
// handlers run (the branch handler's B1 learns it); FR1 / FR2 run on the network thread, which only hands the packets
// on. The slot works on this thread alone: B1 -> B2 around user 2's branch of its own scene, and the detours.
std::atomic<uint32_t> g_logic_tid{0};
thread_local uint32_t t_b_token = pfs::FocusSlots::kRefused;
thread_local uintptr_t t_b_mgr = 0;
std::atomic<uint64_t> g_limited{0}, g_lost{0}, g_lines{0};
constexpr uint64_t kMaxLines = 4000;

struct SrvCall {
    uintptr_t entry = 0, rsp = 0;
    uint64_t id = 0;
    uintptr_t m = 0, found = 0;
    uint32_t user = 0, chr = 0;
    uint8_t who = 0;
    bool r_seen = false;
    bool routed = false;  // U2-b gave user 2's manager
};
thread_local SrvCall t_srv;

// ---------------------------------------------------------------- hot path

// game::fast_read: the base's SEH-guarded direct read (no VirtualQuery). pp57-armwrest (2026-11-24): the focus probe's
// reads through a stage's +0x48 fault on server stages; in this module's own __try each fault was "not a known guarded
// read" for the checklist (its crash capture records every first-chance exception in the mod); fast_read's are known.
template <class T>
bool rd(uintptr_t a, T& v) noexcept {
    if (a < 0x10000) return false;
    T tmp{};
    if (!game::fast_read(a, &tmp, sizeof tmp)) return false;
    v = tmp;
    return true;
}
template <class T>
T rdv(uintptr_t a, T fallback = T{}) noexcept {
    T v{};
    return rd(a, v) ? v : fallback;
}
uintptr_t rdp(uintptr_t a) noexcept { return rdv<uintptr_t>(a); }
// isl::find_stage's reader: the SEH reads above.
template <class T>
bool seh_rd(uintptr_t a, T& v) noexcept {
    return rd(a, v);
}
struct SehRd {
    template <class T>
    bool rd(uintptr_t a, T& v) const noexcept {
        return seh_rd(a, v);
    }
};
// Up to 40 bytes of a stage's name ([[stage+0x48]+0x1B8], a char*) into v[at .. at+4].
void put_stage_name(uintptr_t stage, uint64_t* v, int at) noexcept {
    const uintptr_t info = stage ? rdp(stage + kScene_Info) : 0;
    const uintptr_t name = info ? rdp(info + kInfo_Name) : 0;
    for (int k = 0; k < 5 && name; ++k) v[at + k] = rdv<uint64_t>(name + 8 * static_cast<uintptr_t>(k));
}

uint32_t user_key_of(uintptr_t actor) noexcept {
    const uintptr_t u = actor ? rdp(actor + kActor_User) : 0;
    return u ? rdv<uint32_t>(u + kUser_Key) : 0;
}

void emit(Kind k, p::Event& e) noexcept {
    e.kind = k;
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    if (!g_lim[k].take(e.qpc_us)) {
        g_limited.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!g_ring.push(e)) g_lost.fetch_add(1, std::memory_order_relaxed);
}

uint64_t book_key(uint64_t id, uint8_t tag) noexcept { return (id & 0x00FFFFFFFFFFFFFFull) | static_cast<uint64_t>(tag) << 56; }

Who who_of(uint32_t user, uint32_t chr) noexcept {
    return shp::who(user, chr, server_user2_user_key(), server_user2_player_key());
}

// E 0x288B7A0: rcx = mgr, rdx = field, r8 = the manager's character.
void on_e(safetyhook::Context& ctx) noexcept {
    if (!g_e_on.load(std::memory_order_acquire)) return;
    g_keys.add(rdv<uint32_t>(ctx.r8 + kActor_Key), shp::KeyTable<16>::Entries);
}

// G 0x288B87A: eax = the character's user key (0 without a user), [r15+0x180] = the field's owner.
void on_g(safetyhook::Context& ctx) noexcept {
    if (!g_g_on.load(std::memory_order_acquire)) return;
    const uint32_t chr = rdv<uint32_t>(ctx.r13 + kActor_Key);
    const auto user = static_cast<uint32_t>(ctx.rax);  // the game's own value, logged as it was
    // P8 option U, U-a (user2_stages.h): one hook per address, so option U's gate is asked from here.
    uintptr_t rax = ctx.rax;
    user2_stages_gate(ctx.rdi, ctx.r13, ctx.r15, ctx.rsi, rax);  // 3576: the 4th argument in rsi
    ctx.rax = rax;
    const uint32_t owner = rdv<uint32_t>(ctx.r15 + kField_Owner);
    const bool is_owner = owner == user;
    g_keys.add(chr, shp::KeyTable<16>::Gate, user, owner);
    g_keys.add(chr, is_owner ? shp::KeyTable<16>::Owner : shp::KeyTable<16>::NotOwner, user, owner);
    if (!g_book.take(book_key(chr, static_cast<uint8_t>(0x10 | (is_owner ? 1 : 0))), 2)) return;
    p::Event e;
    e.v[0] = chr;
    e.v[1] = user;
    e.v[2] = owner;
    e.v[3] = ctx.rdi;
    e.v[4] = ctx.r15;
    e.v[5] = is_owner;
    emit(kEGate, e);
}

// P 0x288AAEF: after 0x287C200 for a stage element (rdi). r15 = the manager, r14 = &result.
void on_p(safetyhook::Context& ctx) noexcept {
    if (!g_p_on.load(std::memory_order_acquire)) return;
    g_prep.fetch_add(1, std::memory_order_relaxed);
    const uint32_t result = rdv<uint32_t>(ctx.r15);  // 3576 (2976 r14)
    if (result) g_prep_fail.fetch_add(1, std::memory_order_relaxed);
    const uint32_t owner = rdv<uint32_t>(ctx.rbp + kP_PlayData);
    const uint64_t id = rdv<uint64_t>(ctx.rbp + kP_Id);
    // The resource's path (a char*): its base name, up to 40 bytes, read byte by byte (SEH, at most 160 bytes).
    const uintptr_t res = rdp(ctx.rbx + kElem_Res);  // 3576: the element in rbx
    const uintptr_t path = res ? rdp(rdp(res)) : 0;
    uintptr_t base = path;
    size_t len = 0;
    for (size_t i = 0; path && i < 160; ++i) {
        uint8_t c = 0;
        if (!rd(path + i, c) || c == 0) break;
        if (c == '/' || c == '\\') base = path + i + 1;
        len = i + 1;
    }
    p::Event e;
    size_t n = 0;
    char head[40] = {};  // Co-op+: 40 (was 16; 24 for bgm_cd_seq_quest_): story_gate.h tells cd_seq_quest_intro_0310_xx apart
    for (uintptr_t a = base; path && a < path + len && n < 40; ++a, ++n) {
        uint8_t c = 0;
        if (!rd(a, c)) break;
        e.v[6 + n / 8] |= static_cast<uint64_t>(c) << (8 * (n % 8));
        if (n < sizeof head) head[n] = static_cast<char>(c);
    }
    constexpr char kPrefix[] = "cd_seq_funcnpc_";
    bool funcnpc = n > sizeof kPrefix - 1;  // review B5: as u2sh::is_funcnpc (the bare prefix is no shop)
    for (size_t i = 0; funcnpc && i < sizeof kPrefix - 1; ++i) funcnpc = head[i] == kPrefix[i];
    const uintptr_t mgr_actor = rdp(ctx.r14 + kMgr_Actor);  // 3576: the manager in r14
    const uint32_t mgr_chr = mgr_actor ? rdv<uint32_t>(mgr_actor + kActor_Key) : 0;
    if (funcnpc && g_twin.load(std::memory_order_relaxed)) {
        // S2 probe: would a twin of this host scene be prepared for user 2? Nothing is called or written.
        shp::TwinFacts t;
        t.funcnpc = true;
        t.prepared = result == 0;
        const uint32_t mgr_user = user_key_of(mgr_actor);
        const uint32_t u2_user = server_user2_user_key();
        t.host_scene = owner == mgr_chr && mgr_user && mgr_user != u2_user;
        const uintptr_t u2_char = g_u2_char.load(std::memory_order_relaxed);
        const uint32_t u2_key = g_u2_key.load(std::memory_order_relaxed);
        t.user2_known = u2_char && u2_key && rdv<uint32_t>(u2_char + kActor_Key) == u2_key;
        const uintptr_t u2_mgr = g_u2_mgr.load(std::memory_order_relaxed);
        t.user2_mgr_ok = u2_mgr && rdp(u2_mgr) == g_base + isl::kVtServerStageMgr;
        t.already = g_twinned.has(id);
        const shp::TwinVerdict tv = shp::decide_twin(t);
        g_twin_verdicts[static_cast<size_t>(tv)].fetch_add(1, std::memory_order_relaxed);
        if (tv == shp::TwinVerdict::Would) g_twinned.add(id);
        if (tv != shp::TwinVerdict::Already) {
            p::Event te;
            te.v[0] = static_cast<uint64_t>(tv) | static_cast<uint64_t>(rdv<uint8_t>(u2_mgr + kMgr_State)) << 8;
            te.v[1] = id;
            te.v[2] = u2_mgr;
            te.v[3] = u2_key | static_cast<uint64_t>(owner) << 32;
            for (uint32_t k = 0; k < kPlayDataLogged / 8; ++k) te.v[4 + k] = rdv<uint64_t>(ctx.rbp + kP_PlayData + 8 * k);
            emit(kETwin, te);
        }
    }
    // U2 (user2_shops.h): user 2's own preparations are booked (shop scenes) or checked (anything else).
    // playData +0x20 = the resource's file-name hash (0x288A970: uStack_88, the 2661's @27).
    // Co-op+ 2026-11-13: a story scene (cd_seq_quest_*): user2_shops lets it pass U-f's abort with +quest.
    const bool quest = u2sh::is_quest(std::string_view(head, std::min(n, sizeof head)));
    user2_shops_prepared(ctx.r14, id, funcnpc, n > 0, rdv<uint32_t>(ctx.rbp + kP_PlayData + 0x20), result, quest);
    if (!result) story_gate_note_prepared(ctx.r14, id, rdv<uint32_t>(ctx.rbp + kP_PlayData + 0x20), head, std::min(n, sizeof head));  // Co-op+ the ready check
    if (funcnpc) {
        g_prep_funcnpc.fetch_add(1, std::memory_order_relaxed);
        if (!result) g_funcnpc.add(id);
        if (owner != mgr_chr) g_prep_funcnpc_not_owner.fetch_add(1, std::memory_order_relaxed);
    } else if (!g_book.take(book_key(0xFFFF, 0x20), 24)) {
        return;  // the other stage elements: the first 24 are logged, all counted
    }
    e.v[0] = result | static_cast<uint64_t>(owner) << 32;
    e.v[1] = id;
    e.v[2] = rdp(ctx.rbp + kP_Inst);
    e.v[3] = rdp(ctx.rbx + kElem_Bind);
    e.v[4] = ctx.r14;
    e.v[5] = mgr_chr | static_cast<uint64_t>(user_key_of(mgr_actor)) << 32;
    e.v[11] = funcnpc;
    emit(kEPrep, e);
}

// C0 (BUG 2's talk window), defined with the slot below.
void on_c0(safetyhook::Context& ctx) noexcept;
void slot_talk_leave() noexcept;

// C 0x29E9282: the 2671's server-side result.
void on_c(safetyhook::Context& ctx) noexcept {
    slot_talk_leave();  // first: the host's triple back before anything else runs (C0's window)
    if (!g_c_on.load(std::memory_order_acquire)) return;
    const uint32_t result = rdv<uint32_t>(ctx.rbp + kC_Result);
    const uint32_t requester = rdv<uint32_t>(ctx.rbp + kC_Requester);
    const uint32_t user = user_key_of(ctx.r13);
    const Who w = who_of(user, requester);
    g_2671[static_cast<size_t>(w)].fetch_add(1, std::memory_order_relaxed);
    if (result) g_2671_fail[static_cast<size_t>(w)].fetch_add(1, std::memory_order_relaxed);
    if (w == Who::Host) user2_shops_host_2671(result);  // U2: the HostRefused criterion
    if (w == Who::Host && result == 0) g_host_talk_npc.store(rdv<uint32_t>(ctx.rbp + kC_Npc), std::memory_order_relaxed);
    if (w == Who::User2 && result == 0)  // the catch rule (ai_handoff.h): a caught animal runs on the catcher's game
        ai_handoff_note_catch(requester, rdv<uint32_t>(ctx.rbp + kC_Npc), rdv<uint32_t>(ctx.rbp + kC_Interaction));
    if (w != Who::User2 && !g_book.take(book_key(0x2671, static_cast<uint8_t>(0x30 | static_cast<uint8_t>(w))), 16)) {
        return;
    }
    p::Event e;
    e.v[0] = result;
    e.v[1] = requester;
    e.v[2] = rdv<uint32_t>(ctx.rbp + kC_Npc);
    e.v[3] = rdv<uint32_t>(ctx.rbp + kC_Interaction);
    e.v[4] = user;
    e.v[5] = static_cast<uint64_t>(w);
    emit(kE2671, e);
}

// B1 0x2D55B56: the StageBranchStateReq handler right before its call of the branch 0x2B56870 (rax = the manager).
void slot_branch_enter(uintptr_t mgr, uint64_t seq) noexcept;  // the per-player slot's branch window (below)
void slot_branch_leave() noexcept;
void on_b1(safetyhook::Context& ctx) noexcept {
    if (!g_b1_on.load(std::memory_order_acquire)) return;
    BranchCall& b = t_branch;
    b.seq = rdv<uint64_t>(ctx.rbp + kB_Seq);
    b.then = rdv<uint32_t>(ctx.rbp + kB_Then);
    b.field = rdp(ctx.rbp + kB_Field);
    b.a = rdv<uint8_t>(ctx.rbp + kB_U8a);
    b.b = rdv<uint8_t>(ctx.rbp + kB_U8b);
    b.mgr = ctx.rax;
    b.set = true;
    // 2026-11-25 redo: this handler runs on the server's LOGIC thread (cs125d): the slot's thread.
    g_logic_tid.store(GetCurrentThreadId(), std::memory_order_release);
    slot_branch_enter(b.mgr, b.seq);
}

// B2 0x2D55B6A: the branch's result (!= 0: the handler answers with a Nak 1013).
void on_b2(safetyhook::Context& ctx) noexcept {
    if (!g_b2_on.load(std::memory_order_acquire)) return;
    slot_branch_leave();  // first: the host's triple back before anything else runs
    const uint32_t result = rdv<uint32_t>(ctx.rbp + kB_Result);
    BranchCall b = t_branch;
    t_branch.set = false;
    if (!b.set) g_branch_unpaired.fetch_add(1, std::memory_order_relaxed);
    const bool u2 = b.set && g_u2_branch_seqs.has(b.seq);
    g_branch[u2 ? 0 : 1].fetch_add(1, std::memory_order_relaxed);
    if (result) g_branch_fail[u2 ? 0 : 1].fetch_add(1, std::memory_order_relaxed);
    // Co-op+ 2026-11-24: a refused branch of user 2's own scene that nothing follows ends that scene for user 2
    // (observer_seq_copy_user2_branch_result; Borque's two-PC taming).
    if (u2) observer_seq_copy_user2_branch_result(b.seq, b.then, result);
    // User 2's are all logged (the limiter bounds them); the host's: its failures, 16 per id.
    if (!u2 && (!result || !g_book.take(book_key(b.seq, 0x60), 16))) return;
    p::Event e;
    e.v[0] = result;
    e.v[1] = b.seq;
    e.v[2] = b.then;
    e.v[3] = b.mgr;
    e.v[4] = b.field;
    e.v[5] = static_cast<uint64_t>(b.a) | static_cast<uint64_t>(b.b) << 8 | static_cast<uint64_t>(u2) << 16 |
             static_cast<uint64_t>(b.set) << 17;
    emit(kEBranch, e);
}

// BF1 / BF2: the branch step refused with the code in eax (see kBF1). Whose branch: B1's thread-local on this thread.
void on_bf(safetyhook::Context& ctx, int site) noexcept {
    const BranchCall& b = t_branch;  // B2 clears it after the step returns: still set here
    const bool u2 = b.set && g_u2_branch_seqs.has(b.seq);
    g_bf[site][u2 ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
    // The host's own refusals: the first 16 per site only (book); user 2's: all (the limiter bounds them).
    if (!u2 && !g_book.take(book_key(0xBF00 + static_cast<uint64_t>(site), 0x61), 16)) return;
    const uintptr_t scene = rdp(ctx.rbp + kBF_Scene);
    const uintptr_t node = rdp(ctx.rbp + kBF_Node);
    const uintptr_t owner = scene ? rdp(scene + kBF_SceneOwner) : 0;
    p::Event e;
    uint64_t flag = 0xFF;
    if (site == 0) {
        const uintptr_t elem = rdp(ctx.rbp + kBF_Elem);
        const uintptr_t info = elem ? rdp(elem + kBF_ElemInfo) : 0;
        if (info) flag = rdv<uint8_t>(info + kBF_ElemFlag, 0xFF);
    }
    e.v[0] = static_cast<uint64_t>(site) | flag << 8 | static_cast<uint64_t>(u2) << 16 |
             static_cast<uint64_t>(static_cast<uint32_t>(ctx.rax)) << 32;
    e.v[1] = b.set ? b.seq : 0;
    e.v[2] = scene ? rdv<uint64_t>(scene + kBF_SceneWord) : 0;
    e.v[3] = static_cast<uint64_t>(owner ? rdv<uint32_t>(owner + kActor_Key) : 0) | static_cast<uint64_t>(b.then) << 32;
    for (int i = 0; i < 8; ++i) {
        const uintptr_t q = node ? rdp(node + i * 8) : 0;
        const uintptr_t vt = q > 0x10000 ? rdp(q) : 0;
        e.v[4 + i] = vt >= g_base && vt - g_base < kImageSize3732 ? (static_cast<uint64_t>(vt - g_base) | (1ull << 63))
                                                                 : static_cast<uint64_t>(q);
    }
    emit(kEBranchFail, e);

    // Why (see kBF_SceneProc): the manager's exclusive check and the node's condition object.
    const auto rva = [](uintptr_t a) -> uint64_t {
        return a >= g_base && a - g_base < kImageSize3732 ? static_cast<uint64_t>(a - g_base) : 0;
    };
    const uintptr_t mgr = owner;  // [scene+0x28]
    const uint32_t node_id = node ? rdv<uint32_t>(node + kNode_EntryId) : 0;
    uint8_t excl = 0xFF;  // 0xFF no entry for the node, 0xFE its info unreadable, else the flag
    if (scene && node) {
        const uintptr_t proc = rdp(scene + kBF_SceneProc);
        const uintptr_t arr = proc ? rdp(proc + kProc_Entries) : 0;
        const uint32_t n = proc ? rdv<uint32_t>(proc + kProc_Count) : 0;
        for (uint32_t i = 0; arr && i < n && i < kMaxProcEntries; ++i) {
            const uintptr_t en = rdp(arr + 8ull * i);
            if (!en || rdv<uint32_t>(en, 0xFFFFFFFFu) != node_id) continue;
            const uintptr_t info = rdp(en + kBF_ElemInfo);
            excl = info ? rdv<uint8_t>(info + kBF_ElemFlag, uint8_t{0xFE}) : uint8_t{0xFE};
            break;
        }
    }
    const uint64_t a0 = mgr ? rdv<uint64_t>(mgr + stp::kMgr_Current) : 0;
    const uint64_t m90 = mgr ? rdv<uint64_t>(mgr + stp::kMgr_Current2) : 0;
    const uint64_t cur = stp::current_of(a0, m90);
    isl::Lookup look;
    if (mgr && (cur & stp::kStageMask)) look = isl::find_stage(SehRd{}, mgr, cur);
    const uintptr_t data = node ? rdp(node) : 0;
    const uintptr_t holder = data ? rdp(data + kNodeData_Cond) : 0;
    const uintptr_t cond = holder ? rdp(holder) : 0;
    const uintptr_t cvt = cond ? rdp(cond) : 0;
    p::Event w;
    w.v[0] = static_cast<uint64_t>(site) | static_cast<uint64_t>(u2) << 8 | static_cast<uint64_t>(excl) << 16 |
             static_cast<uint64_t>(node_id) << 32;
    w.v[1] = b.set ? b.seq : 0;
    w.v[2] = scene ? rdv<uint64_t>(scene + kBF_SceneWord) : 0;
    w.v[3] = mgr;
    w.v[4] = b.set ? b.mgr : 0;
    w.v[5] = m90;
    w.v[6] = a0;
    w.v[7] = (scene ? rdv<uint8_t>(scene + kScene_Type) : 0) |
             static_cast<uint64_t>(scene ? rdv<uint32_t>(scene + kScene_Group) : 0) << 32;
    w.v[8] = static_cast<uint64_t>(look.read_ok) | static_cast<uint64_t>(look.found) << 1 |
             static_cast<uint64_t>(look.dead) << 2 |
             static_cast<uint64_t>(look.found ? rdv<uint32_t>(look.inst + kScene_Group) : 0) << 32;
    w.v[9] = rva(cvt);
    w.v[10] = cvt ? (rva(rdp(cvt + 8)) | rva(rdp(cvt + 0x10)) << 32) : 0;
    w.v[11] = mgr ? rva(rdp(mgr)) : 0;
    emit(kEBranchWhy, w);
    p::Event nm;
    put_stage_name(look.found ? look.inst : 0, nm.v, 0);
    put_stage_name(scene, nm.v, 5);
    nm.v[10] = cur;
    nm.v[11] = w.v[2];
    emit(kEBranchNames, nm);
}
void on_bf1(safetyhook::Context& ctx) noexcept {
    if (g_bf1_on.load(std::memory_order_acquire)) on_bf(ctx, 0);
}
void on_bf2(safetyhook::Context& ctx) noexcept {
    if (g_bf2_on.load(std::memory_order_acquire)) on_bf(ctx, 1);
}

// ---- the per-player focus slot's helpers (see kFSetFn)
template <class T>
bool wr(uintptr_t a, T v) noexcept {
    if (a < 0x10000) return false;
    __try {
        *reinterpret_cast<volatile T*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
struct MgrMem {  // pfs::FocusSlots' Mem: the manager's +0x90 / +0x98 / +0xA0
    uintptr_t m = 0;
    bool read(pfs::Triple& t) noexcept {
        return rd(m + kMgr_Focus90, t.f90) && rd(m + kMgr_Focus98, t.f98) && rd(m + kMgr_FocusA0, t.fa0);
    }
    bool write(const pfs::Triple& t) noexcept {
        return wr(m + kMgr_Focus90, t.f90) && wr(m + kMgr_Focus98, t.f98) && wr(m + kMgr_FocusA0, t.fa0);
    }
};
// A stage's owner for the slot ([[stage+0x48]+0x60]): user 2's character, the host's, or neither (an NPC's scene,
// an unreadable one).
enum class StageOwner : uint8_t { Other, Host, User2 };
std::atomic<uint32_t> g_host_key{0};  // the host's character key (host_mgr_live)
// pp57-armwrest: no owner field was found on a server stage (+0x98 and [+0x48]+0x60 read 0 on the host's own talk), so
// the owner test is the id: user 2's own scenes are the ones the server prepared for user 2 naming its character
// (g_u2_own, from the PrepareSequencerAcks); any other stage in the host's manager is the host's (the manager's owner).
StageOwner stage_owner(uintptr_t stage) noexcept {
    if (!stage) return StageOwner::Other;
    const uint64_t id = rdv<uint64_t>(stage + kBF_SceneWord);
    if (!id) return StageOwner::Other;
    return g_u2_own.has(id) ? StageOwner::User2 : StageOwner::Host;
}
// The host's ServerSequencerStageManager read LIVE (the chain refresh_user2 follows, from the host's user), its vtable
// checked; 0 when a link is missing. Review 2026-11-24 #4: never write into a cached manager that may have gone.
uintptr_t host_mgr_live(uint32_t* key = nullptr) noexcept {
    uintptr_t user = 0;
    if (!server_host_user(user) || !user) return 0;
    const uintptr_t chr = rdp(user + kUser_Main);
    const uintptr_t comps = chr ? rdp(chr + kActor_Comps) : 0;
    const uintptr_t seq = comps ? rdp(comps + kComps_Seq) : 0;
    const uintptr_t mgr = seq ? rdp(seq + kSeqComp_Mgr) : 0;
    if (!mgr || rdp(mgr) != g_base + isl::kVtServerStageMgr) return 0;
    if (key) *key = rdv<uint32_t>(chr + kActor_Key);
    return mgr;
}
// Between two pieces of work (depth 0): the resets the module's thread asked for.
void slot_housekeeping() noexcept {
    if (g_slots.depth() != 0) return;
    if (g_slot_reset_all.exchange(false, std::memory_order_acq_rel)) {
        g_slots = pfs::FocusSlots{};  // another host manager: nothing kept applies to it
        g_slot_reset_u2.store(false, std::memory_order_relaxed);
        g_slot_resets.fetch_add(1, std::memory_order_relaxed);
    }
    if (g_slot_reset_u2.exchange(false, std::memory_order_acq_rel)) g_slots.reset_user2();
}
uintptr_t s_slot_mgr = 0;  // the manager of the open pieces of work (the server's thread only)
std::atomic<uint64_t> g_slot_unwinds{0}, g_slot_dropped{0}, g_slot_host_in_u2{0};
uint32_t slot_enter(uintptr_t mgr, pfs::Owner o) noexcept {
    slot_housekeeping();
    if (g_slots.depth() == 0) s_slot_mgr = mgr;
    MgrMem mem{mgr};
    return g_slots.enter(mem, o);
}
void slot_leave(uintptr_t mgr, uint32_t token) noexcept {
    MgrMem mem{mgr};
    g_slots.leave(mem, token);
}
// Around a piece of work, also when an exception passes through it (review 2026-11-24 #1).
struct SlotScope {
    uintptr_t mgr = 0;
    uint32_t token = pfs::FocusSlots::kRefused;
    SlotScope(uintptr_t m, pfs::Owner o) noexcept : mgr(m), token(slot_enter(m, o)) {}
    ~SlotScope() { slot_leave(mgr, token); }
    SlotScope(const SlotScope&) = delete;
    SlotScope& operator=(const SlotScope&) = delete;
};
// The fix is on and this is the server's thread (the one that hands the requests to ServerNet::OnRecvPacket: g_slots
// has no lock). Before user 2's session exists the thread is not known: nothing is done, nothing counted.
std::atomic<uint64_t> g_slot_other_thread{0};
bool slot_here() noexcept {
    if (!g_slot_on.load(std::memory_order_acquire)) return false;
    // The logic thread (B1 learns it): until a branch was seen, nothing is done here (the game's own slot as it is).
    const uint32_t tid = g_logic_tid.load(std::memory_order_acquire);
    if (!tid) return false;
    if (GetCurrentThreadId() != tid) {
        g_slot_other_thread.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
// The manager the detours apply the slot to (the host's), or 0: the call goes on as it is.
uintptr_t slot_mgr() noexcept { return slot_here() ? g_host_mgr.load(std::memory_order_acquire) : 0; }
// The outermost point of the server's work: whatever a missed leave left open is closed here, if its manager is still
// a manager (else the slot's state is dropped: nothing to give back to a manager that went away).
void slot_outermost() noexcept {
    if (g_slots.depth() == 0 && g_slots.current() == pfs::Owner::Host) return;
    g_slot_unwinds.fetch_add(1, std::memory_order_relaxed);
    if (s_slot_mgr && rdp(s_slot_mgr) == g_base + isl::kVtServerStageMgr) {
        MgrMem mem{s_slot_mgr};
        (void)g_slots.unwind(mem);
        return;
    }
    g_slot_dropped.fetch_add(1, std::memory_order_relaxed);
    g_slots = pfs::FocusSlots{};
}

// The per-player slot's branch window (2026-11-25 redo, on the LOGIC thread): B1 -> B2 of the StageBranchStateReq
// handler, around its call of the branch step (the condition check that reads the focus), when the branch is user 2's
// of a scene of its OWN (its id among the 2356-learned ones) in the HOST's manager. Only with B2 hooked (a window that
// never closes: none). Its other branches and the host's run with the host's triple, as without the fix.
void slot_branch_enter(uintptr_t mgr, uint64_t seq) noexcept {
    t_b_mgr = 0;
    t_b_token = pfs::FocusSlots::kRefused;
    if (!g_b2_on.load(std::memory_order_acquire) || !slot_here()) return;
    slot_outermost();  // a leave missed earlier on this thread: closed before this piece of work
    if (!seq || !g_u2_own.has(seq)) {
        g_slot_u2_passed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    uint32_t key = 0;
    const uintptr_t hm = host_mgr_live(&key);
    if (!hm || mgr != hm) {  // its own scene in its OWN manager: its focus is apart already
        g_slot_u2_passed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Review 2026-11-25 (pp59): the first to see another host manager (a reload) asks the reset itself.
    if (g_host_mgr.exchange(hm, std::memory_order_acq_rel) != hm)
        g_slot_reset_all.store(true, std::memory_order_release);
    if (key) g_host_key.store(key, std::memory_order_relaxed);
    t_b_mgr = hm;
    t_b_token = slot_enter(hm, pfs::Owner::User2);
    g_slot_u2[0].fetch_add(1, std::memory_order_relaxed);
}
void slot_branch_leave() noexcept {
    if (t_b_mgr) slot_leave(t_b_mgr, t_b_token);
    t_b_mgr = 0;
    t_b_token = pfs::FocusSlots::kRefused;
}

// BUG 2's talk window (2026-11-25, on the LOGIC thread): C0 -> C of the InteractionPreemptionStepReq 2642 handler,
// around its path check 0x2E3C180, for user 2's talk (the requester actor r13 is its character): the check runs with
// user 2's own focus triple in the host's manager. Why: while the host is in a talk (its talk's stage holds the host
// manager's focus), every talk of the friend's was refused with 0x58C8345D, which is neither preempt error (cs127c's
// values) and comes from inside that check; its stage-chart condition (0x2126920) reads the FIELD OWNER's manager. The
// earlier try of this (0f4af72, cs125b) ran on the network thread and tested nothing (cs125d). The NPC of the host's
// own talk, while the host's focus is set, is left to the game (its one-talker rule).
void on_c0(safetyhook::Context& ctx) noexcept {
    t_c_mgr = 0;
    t_c_token = pfs::FocusSlots::kRefused;
    if (!g_c0_on.load(std::memory_order_acquire) || !slot_here()) return;
    slot_outermost();  // a leave missed earlier on this thread: closed before this piece of work
    const uint32_t user = user_key_of(ctx.r13);
    if (!user || user != server_user2_user_key()) return;  // the host's (or anyone else's) talk: as it is
    uint32_t key = 0;
    const uintptr_t hm = host_mgr_live(&key);
    if (!hm) return;
    const uint32_t npc = rdv<uint32_t>(ctx.rbp + kC_Npc);
    const bool host_in_focus = rdv<uint64_t>(hm + kMgr_FocusA0) != 0 || rdv<uint64_t>(hm + kMgr_Focus90) != 0;
    if (npc && npc == g_host_talk_npc.load(std::memory_order_relaxed) && host_in_focus) {
        g_slot_talks_left.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (g_host_mgr.exchange(hm, std::memory_order_acq_rel) != hm) g_slot_reset_all.store(true, std::memory_order_release);
    if (key) g_host_key.store(key, std::memory_order_relaxed);
    t_c_mgr = hm;
    t_c_token = slot_enter(hm, pfs::Owner::User2);
    g_slot_talks.fetch_add(1, std::memory_order_relaxed);
}
void slot_talk_leave() noexcept {
    if (t_c_mgr) slot_leave(t_c_mgr, t_c_token);
    t_c_mgr = 0;
    t_c_token = pfs::FocusSlots::kRefused;
}

// The focus probe (see kFS). FR: whose request the server loop is handing to ServerNet::OnRecvPacket. This is the
// NETWORK thread (cs125d): it only hands the packets on; the handlers and the focus writes run on the logic thread, so
// the per-player slot does nothing here any more (2026-11-25 redo; the counters stay).
void on_fr1(safetyhook::Context& ctx) noexcept {
    if (!g_fr1_on.load(std::memory_order_acquire)) return;
    // cs121a: user 2's requests come here with the HOST's session in r8 ([r15+0x18]); OnRecvPacket's entry hook
    // re-routes them by their buffer's tag. So whose request it is: the buffer (r9, set at 0x2A10B78), as that hook.
    t_recv_any = 1;
    t_recv_u2 = server_packet_is_user2(ctx.r9) ? 1u : 0u;
    g_recv_all.fetch_add(1, std::memory_order_relaxed);
    if (t_recv_u2) g_recv_u2.fetch_add(1, std::memory_order_relaxed);
}
void on_fr2(safetyhook::Context&) noexcept {
    t_recv_u2 = 0;
    t_recv_any = 0;
}
// what: 0 set (FS), 1 undo (FU), 2 clear at a stage's end (FD).
void focus_event(uint32_t what, uintptr_t mgr, uint64_t new_id, uintptr_t scene, uint64_t ret) noexcept {
    // 2026-11-25 redo (cs125d): the focus is written on the LOGIC thread, where FR's flags (the network thread) are
    // never set. "Inside a request" is now inside a branch handler's step (B1 -> B2) on this thread; user 2's when its
    // sequencer is one of user 2's branch sequencers.
    const BranchCall& br = t_branch;
    const bool in_branch = br.set;
    const bool u2req = in_branch && g_u2_branch_seqs.has(br.seq);
    g_focus_n[what][u2req ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
    const auto rva = [](uint64_t a) -> uint64_t {
        return a >= g_base && a - g_base < kImageSize3732 ? a - g_base : 0;
    };
    const uintptr_t u2mgr = g_u2_mgr.load(std::memory_order_relaxed);
    p::Event e;
    e.v[0] = static_cast<uint64_t>(what) | static_cast<uint64_t>(u2req) << 8 | static_cast<uint64_t>(in_branch) << 9 |
             static_cast<uint64_t>(mgr && mgr == u2mgr) << 10 |
             static_cast<uint64_t>(scene && g_u2_own.has(rdv<uint64_t>(scene + kBF_SceneWord))) << 11;
    e.v[1] = mgr;
    e.v[2] = mgr ? rdv<uint64_t>(mgr + kMgr_Focus90) : 0;
    e.v[3] = mgr ? rdv<uint64_t>(mgr + kMgr_Focus98) : 0;
    e.v[4] = mgr ? rdv<uint64_t>(mgr + kMgr_FocusA0) : 0;
    e.v[5] = new_id;
    e.v[6] = scene;
    e.v[7] = scene ? rdv<uint64_t>(scene + kBF_SceneWord) : 0;
    const uintptr_t data = scene ? rdp(scene + kScene_Info) : 0;
    e.v[8] = static_cast<uint64_t>(data ? rdv<uint32_t>(data + kStageData_Owner) : 0) |
             static_cast<uint64_t>(scene ? rdv<uint32_t>(scene + kStage_Owner98) : 0) << 32;
    e.v[9] = rva(ret) | static_cast<uint64_t>(server_user2_player_key()) << 32;
    const uintptr_t name = data ? rdp(data + kInfo_Name) : 0;
    e.v[10] = name ? rdv<uint64_t>(name) : 0;
    e.v[11] = name ? rdv<uint64_t>(name + 8) : 0;
    emit(kEFocus, e);
}
void on_fs(safetyhook::Context& ctx) noexcept {
    if (!g_fs_on.load(std::memory_order_acquire)) return;
    const uintptr_t holder = ctx.r14;
    focus_event(0, ctx.rcx, ctx.rdx, holder ? rdp(holder + kFS_SceneHolder) : 0, 0);
}
void on_fu(safetyhook::Context& ctx) noexcept {
    if (!g_fu_on.load(std::memory_order_acquire)) return;
    focus_event(1, ctx.rcx, 0, 0, rdv<uint64_t>(ctx.rsp));  // at the entry: [rsp] is the return address
}
// Every stage's end comes to 0x21CB970 (hundreds a minute in a town): only the end of the manager's focus stage is an
// event (0x21CB970's own test: the stage's +0x30 against the focus, +0xA0 when set, else +0x90).
void fd_check(uintptr_t mgr, uintptr_t stage, uint64_t ret) noexcept {
    const uint64_t cur = mgr ? stp::current_of(rdv<uint64_t>(mgr + kMgr_FocusA0), rdv<uint64_t>(mgr + kMgr_Focus90)) : 0;
    if (!stage || !(cur & stp::kStageMask) || cur != rdv<uint64_t>(stage + kBF_SceneWord)) return;
    focus_event(2, mgr, 0, stage, ret);
}
void on_fd(safetyhook::Context& ctx) noexcept {
    if (!g_fd_on.load(std::memory_order_acquire)) return;
    fd_check(ctx.rcx, ctx.rdx, rdv<uint64_t>(ctx.rsp));
}

// The per-player slot's detours (inline hooks at the three entries; see kFSetFn). In the host's manager on the server's
// thread: a scene of user 2's own (its id in g_u2_own) works with user 2's triple, any other stage with the host's
// (that matters inside user 2's request). A scene of user 2's whose PrepareSequencerAck was missed falls back to the
// game's own shared slot for its tick-time work while its requests use user 2's triple: the [focus] lines (scenes
// marked "USER 2's") are checked before the fix is used (review 2026-11-24 #6). pp57-armwrest: the focus is set in
// the processor's tick (outside any request), so the stage test, not the request window, carries most of the fix.
// Returns: whatever the original leaves in rax (none of the three returns in xmm0).
// The owner a piece of work in the host's manager runs as, or false: as it is (no swap needed or no owner known).
bool slot_owner_for(uintptr_t stage, pfs::Owner& o) noexcept {
    const StageOwner so = stage_owner(stage);
    if (so == StageOwner::User2) {
        o = pfs::Owner::User2;
        return true;
    }
    if (so == StageOwner::Host && g_slots.current() != pfs::Owner::Host) {
        o = pfs::Owner::Host;
        g_slot_host_in_u2.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}
uint64_t __fastcall fset_detour(uintptr_t proc, uint64_t a2, uint64_t a3, uint64_t a4) {
    const uintptr_t host = slot_mgr();
    const uintptr_t mgr = host ? rdp(proc + kProc_Mgr8) : 0;
    pfs::Owner o{};
    if (!host || mgr != host || !slot_owner_for(rdp(proc + kProc_Stage10), o)) {
        if (host && mgr != host) g_slot_other.fetch_add(1, std::memory_order_relaxed);
        return g_fset_hook.unsafe_call<uint64_t>(proc, a2, a3, a4);
    }
    if (o == pfs::Owner::User2) g_slot_u2[1].fetch_add(1, std::memory_order_relaxed);
    SlotScope s(mgr, o);
    return g_fset_hook.unsafe_call<uint64_t>(proc, a2, a3, a4);
}
uint64_t __fastcall ftick_detour(uintptr_t proc, uint64_t a2, uint64_t a3, uint64_t a4) {
    const uintptr_t host = slot_mgr();
    const uintptr_t mgr = host ? rdp(proc + kProc_Mgr8) : 0;
    pfs::Owner o{};
    if (!host || mgr != host || !slot_owner_for(rdp(proc + kProc_Stage10), o)) {
        return g_ftick_hook.unsafe_call<uint64_t>(proc, a2, a3, a4);
    }
    if (o == pfs::Owner::User2) g_slot_u2[3].fetch_add(1, std::memory_order_relaxed);
    SlotScope s(mgr, o);
    return g_ftick_hook.unsafe_call<uint64_t>(proc, a2, a3, a4);
}
uint64_t __fastcall fend_detour(uintptr_t mgr, uintptr_t stage, uint64_t a3, uint64_t a4) {
    const uint64_t ret = reinterpret_cast<uint64_t>(_ReturnAddress());
    const bool log_end = g_fd_on.load(std::memory_order_relaxed);
    const uintptr_t host = slot_mgr();
    pfs::Owner o{};
    if (!host || mgr != host || !slot_owner_for(stage, o)) {
        if (log_end) fd_check(mgr, stage, ret);
        return g_fend_hook.unsafe_call<uint64_t>(mgr, stage, a3, a4);
    }
    if (o == pfs::Owner::User2) g_slot_u2[2].fetch_add(1, std::memory_order_relaxed);
    SlotScope s(mgr, o);
    if (log_end) fd_check(mgr, stage, ret);  // against the triple this stage's end really meets (review #5)
    return g_fend_hook.unsafe_call<uint64_t>(mgr, stage, a3, a4);
}

// S: the server's 0x2126920. A = the decision point's registers (RAX = field vf48), R = the lookup's result, F = BL.
void on_sa(safetyhook::Context& ctx) noexcept {
    if (!g_sa_on.load(std::memory_order_acquire)) return;
    const uintptr_t field = rdp(ctx.rsp + isl::kRspField);
    if (!field || rdp(field) != g_base + isl::kVtServerField) return;  // the host client's own prompt: not ours
    const uint32_t user = user_key_of(ctx.r15);
    const uint32_t chr = rdv<uint32_t>(ctx.r15 + kActor_Key);
    const Who w = who_of(user, chr);
    g_srv[static_cast<size_t>(w)].fetch_add(1, std::memory_order_relaxed);
    t_srv = SrvCall{};
    t_srv.entry = ctx.r13;
    t_srv.rsp = ctx.rsp;
    t_srv.id = rdv<uint64_t>(ctx.r13 + isl::kEntry_StageId);
    t_srv.m = ctx.rax;
    t_srv.user = user;
    t_srv.chr = chr;
    t_srv.who = static_cast<uint8_t>(w);
    // U2-b (user2_shops.h): an entry of a scene of user 2's own manager is checked against that manager, whoever asks
    // (RAX := it; the two relocated instructions copy it to rdi and [rbp+0x8E8], so the lock, the lookup and the dead
    // check use it alike, as C1 does on the guest).
    if (const uintptr_t mine =
            user2_shops_server_check(field, t_srv.id, ctx.rax, w == Who::Host, w == Who::User2)) {
        ctx.rax = mine;
        t_srv.m = mine;
        t_srv.routed = true;
    }
}
void on_sr(safetyhook::Context& ctx) noexcept {
    if (!g_sr_on.load(std::memory_order_acquire)) return;
    if (!t_srv.entry || t_srv.entry != ctx.r13 || t_srv.rsp != ctx.rsp) return;
    t_srv.found = ctx.rsi;
    t_srv.r_seen = true;
    if (ctx.rsi) g_srv_found[t_srv.who % 3].fetch_add(1, std::memory_order_relaxed);
}
void on_sf(safetyhook::Context& ctx) noexcept {
    if (!g_sf_on.load(std::memory_order_acquire)) return;
    if (!t_srv.entry || t_srv.entry != ctx.r13 || t_srv.rsp != ctx.rsp) return;
    const SrvCall c = t_srv;
    t_srv = SrvCall{};
    const auto bl = static_cast<uint8_t>(ctx.rbx & 0xFF);
    g_srv_result[c.who % 3][bl <= 2 ? bl : 3].fetch_add(1, std::memory_order_relaxed);
    const auto w = static_cast<Who>(c.who);
    if (!g_book.take(book_key(c.id, static_cast<uint8_t>(0x40 | c.who << 2 | (bl <= 2 ? bl : 3))),
                     w == Who::User2 ? 8 : 2)) {
        return;
    }
    p::Event e;
    e.v[0] = c.who | static_cast<uint64_t>(bl) << 8 | static_cast<uint64_t>(c.r_seen) << 16 |
             static_cast<uint64_t>(c.routed) << 17;
    e.v[1] = c.id;
    e.v[2] = c.entry;
    e.v[3] = c.m;
    e.v[4] = c.found;
    e.v[5] = c.user | static_cast<uint64_t>(c.chr) << 32;
    emit(kESrv, e);
}

// ---------------------------------------------------------------- lines (the module's thread)

std::string name_of(const uint64_t* q, size_t nq) {
    std::string s;
    for (size_t i = 0; i < nq * 8; ++i) {
        const auto c = static_cast<unsigned char>(q[i / 8] >> (8 * (i % 8)));
        if (!c) break;
        s += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    return s;
}

std::string hex_of(const uint64_t* q, size_t nbytes) {
    std::string s;
    for (size_t i = 0; i < nbytes; ++i) {
        s += std::format("{}{:02X}", i ? " " : "", static_cast<unsigned>((q[i / 8] >> (8 * (i % 8))) & 0xFF));
    }
    return s;
}

const char* req_name(uint16_t op) {
    switch (op) {
        case shp::kOpPreemptionReq: return "3008 InteractionPreemptionStepReq";
        case shp::kOpDoStepReq: return "3073 InteractionDoStepReq";
        case shp::kOpBranchReq: return "2880 StageBranchStateReq";
        case shp::kOpChartEvent: return "2591 (stage chart event Req)";
        case shp::kOpPreemptionOk: return "2120 InteractionPreemptionStepSuccessAck";
        case shp::kOpPreemptionFail: return "3100 InteractionPreemptionStepFailAck";
        case shp::kOpDoStepOk: return "2794 InteractionDoStepSuccessAck";
        case shp::kOpDoInteraction: return "2525 (the interaction ran)";
        case shp::kOpBranchAck: return "2920 StageBranchStateAck";
        case shp::kOpOnBranchEvent: return "2436 StageOnBranchEventAck";
        default: return "?";
    }
}

// The RTTI class name of an exe vtable (MSVC x64: [vt-8] the complete object locator, signature 1, its type
// descriptor's rva at +0x0C, the decorated name at the descriptor +0x10); on the log thread; "?" when unreadable.
std::string rtti_name(uint64_t vt_rva) {  // game::rtti_name_of_vtable from an exe-relative vtable
    if (!g_base || vt_rva < 8 || vt_rva >= kImageSize3732) return "?";
    const std::string s = game::rtti_name_of_vtable(g_base + static_cast<uintptr_t>(vt_rva));
    return s.empty() ? std::string("?") : s;
}
std::string line_of(const p::Event& e) {
    const uint64_t* v = e.v;
    const std::string when = p8_when(e.qpc_us);
    switch (e.kind) {
        case kEGate:
            return std::format("ShopProbe [gate] {} T{} | the stage update 0x288B7A0 of character 0x{:08X} (manager 0x{:X}): "
                               "its user 0x{:08X} vs the field 0x{:X}'s owner 0x{:08X} -> {}",
                               when, e.tid, static_cast<uint32_t>(v[0]), v[3], static_cast<uint32_t>(v[1]), v[4],
                               static_cast<uint32_t>(v[2]),
                               v[5] ? "OWNER: its stages are evaluated (shops, schedules, factions...)"
                                    : "NOT the owner: leaves without evaluating any stage (0x288BDEE)");
        case kEPrep:
            return std::format("ShopProbe [{}] {} T{} | stage element prepared by 0x287C200: '{}' sequencer id 0x{:X}, owner "
                               "0x{:08X}, result 0x{:X}{} | instance 0x{:X}, binding data 0x{:X} | manager 0x{:X} of "
                               "character 0x{:08X} (user 0x{:08X})",
                               v[11] ? "funcnpc" : "stage", when, e.tid, name_of(v + 6, 5), v[1],
                               static_cast<uint32_t>(v[0] >> 32), static_cast<uint32_t>(v[0]),
                               static_cast<uint32_t>(v[0]) ? " (FAILED)" : "", v[2], v[3], v[4],
                               static_cast<uint32_t>(v[5]), static_cast<uint32_t>(v[5] >> 32));
        case kE2671:
            return std::format("ShopProbe [2671] {} T{} | {} asked: requester 0x{:08X} (user 0x{:08X}) -> NPC 0x{:08X} "
                               "interaction 0x{:08X} | 0x2B36EF0 path result 0x{:X} -> {}",
                               when, e.tid, shp::who_name(static_cast<Who>(v[5])), static_cast<uint32_t>(v[1]),
                               static_cast<uint32_t>(v[4]), static_cast<uint32_t>(v[2]), static_cast<uint32_t>(v[3]),
                               static_cast<uint32_t>(v[0]),
                               v[0] ? "REFUSED (the handler answers with its failure)" : "ACCEPTED (2663 follows)");
        case kESrv: {
            const auto w = static_cast<Who>(v[0] & 0xFF);
            const auto bl = static_cast<uint8_t>((v[0] >> 8) & 0xFF);
            return std::format("ShopProbe [server check] {} T{} | 0x2126920 on the SERVER for {} (user 0x{:08X}, "
                               "character 0x{:08X}): entry 0x{:X} stage id 0x{:X} looked up in manager 0x{:X} ({}): {} "
                               "| result {}",
                               when, e.tid, shp::who_name(w), static_cast<uint32_t>(v[5]),
                               static_cast<uint32_t>(v[5] >> 32), v[2], v[1], v[3],
                               (v[0] >> 17) & 1 ? "user 2's own, ROUTED by U2-b" : "the field owner's, ServerField::vf48",
                               !((v[0] >> 16) & 1) ? std::string("R did not run")
                               : v[4]              ? std::format("FOUND (instance 0x{:X})", v[4])
                                                   : std::string("NOT found"),
                               bl == 0 ? "0 (refused)" : bl == 1 ? "1 (accepted)" : bl == 2 ? "2 (accepted)" : "?");
        }
        case kEReq: {
            const auto op = static_cast<uint16_t>(v[0] & 0xFFFF);
            const bool filtered = (v[0] >> 16) & 1, funcnpc = (v[0] >> 17) & 1;
            if (op == shp::kOpPreemptionReq || op == shp::kOpDoStepReq) {
                return std::format("ShopProbe [user2 req] {} T{} | {}: requester 0x{:08X} -> NPC 0x{:08X} interaction "
                                   "0x{:08X} | {}",
                                   when, e.tid, req_name(op), static_cast<uint32_t>(v[1]), static_cast<uint32_t>(v[2]),
                                   static_cast<uint32_t>(v[3]), filtered ? "DROPPED by a filter" : "goes to the server");
            }
            return std::format("ShopProbe [user2 req] {} T{} | {}: sequencer 0x{:X} (then 0x{:08X}){} | observer_seq_copy "
                               "R1: {}",
                               when, e.tid, req_name(op), v[1], static_cast<uint32_t>(v[2]),
                               funcnpc ? ", a cd_seq_funcnpc_* scene of the host" : "",
                               filtered ? "DROPPED (a host sequencer: never reaches the server)"
                                        : "passed (reaches the server: ServerField::vf48 = the host's manager)");
        }
        case kEOut: {
            const auto op = static_cast<uint16_t>(v[0] & 0xFFFF);
            const bool pushed = (v[0] >> 16) & 1, funcnpc = (v[0] >> 17) & 1;
            return std::format("ShopProbe [to user2] {} T{} | {}{} {} | {} bytes: {}", when, e.tid, req_name(op),
                               funcnpc ? " (a cd_seq_funcnpc_* scene)" : "", pushed ? "queued" : "NOT queued", v[4],
                               hex_of(v + 1, std::min<uint64_t>(v[4], 24)));
        }
        case kETwin: {
            const auto tv = static_cast<shp::TwinVerdict>(v[0] & 0xFF);
            return std::format("FuncnpcTwin [{}] {} T{} | host scene id 0x{:X} (owner 0x{:08X}): {} | user 2 character "
                               "0x{:08X}, manager 0x{:X} (state +0x6A0 = {}) | the twin's playData would be this one with "
                               "+0 = 0x{:08X} and +8 = 0: {} | probe: nothing called, nothing written",
                               tv == shp::TwinVerdict::Would ? "would create" : "no twin", when, e.tid, v[1],
                               static_cast<uint32_t>(v[3] >> 32), shp::twin_verdict_name(tv),
                               static_cast<uint32_t>(v[3]), v[2], (v[0] >> 8) & 0xFF, static_cast<uint32_t>(v[3]),
                               hex_of(v + 4, kPlayDataLogged));
        }
        case kEBranch:
            return std::format("ShopProbe [branch] {} T{} | the server's StageBranchStateReq handler for {}: sequencer "
                               "0x{:X} then 0x{:08X} (u8 {} {}) in manager 0x{:X} (field 0x{:X}) | 0x2B56870 result 0x{:X} "
                               "-> {}{}",
                               when, e.tid, (v[5] >> 16) & 1 ? "USER 2" : "the host / another", v[1],
                               static_cast<uint32_t>(v[2]), v[5] & 0xFF, (v[5] >> 8) & 0xFF, v[3], v[4],
                               static_cast<uint32_t>(v[0]),
                               v[0] ? "REFUSED (a Nak 1013 follows)" : "ACCEPTED (its Ack 2920 is the branch's own)",
                               (v[5] >> 17) & 1 ? "" : " (B1 not seen on this thread: sequencer unknown)");
        case kEBranchFail: {
            std::string q;
            for (int i = 4; i < 12; ++i)
                q += (v[i] >> 63) ? std::format(" vt exe+0x{:X}", v[i] & 0x7FFFFFFFFFFFFFFFull) : std::format(" 0x{:X}", v[i]);
            return std::format("ShopProbe [branch fail] {} T{} | the branch step 0x2C0B8B0 refused {}'s branch of sequencer "
                               "0x{:X} to 0x{:08X} with 0x{:08X} at {} | scene +0x30 0x{:X}, [[scene+0x28]+0x60] 0x{:08X} "
                               "(+0x28 is the scene's manager, not an actor) | target node:{}",
                               when, e.tid, (v[0] >> 16) & 1 ? "USER 2" : "the host / another", v[1],
                               static_cast<uint32_t>(v[3] >> 32), static_cast<uint32_t>(v[0] >> 32),
                               (v[0] & 0xFF) == 0 ? std::format("2C0BFD7 (a node entry of kind 2; the element's flag "
                                                                "+0x75 = {})", (v[0] >> 8) & 0xFF)
                                                  : std::string("2C0C03A (the condition call 0x25C82A0 said no)"),
                               v[2], static_cast<uint32_t>(v[3]), q);
        }
        case kEBranchWhy: {
            const auto site = static_cast<uint32_t>(v[0] & 0xFF), excl = static_cast<uint32_t>((v[0] >> 16) & 0xFF),
                       node_id = static_cast<uint32_t>(v[0] >> 32);
            const uint64_t cur = stp::current_of(v[6], v[5]);
            const bool live = (v[8] >> 1) & 1;
            const shp::Excl verdict = shp::exclusive_verdict(cur, live, v[2]);
            const std::string ex = excl == 0xFF ? std::string("the node has no entry in the scene's list")
                                   : excl == 0xFE ? std::string("its entry's info is unreadable")
                                                  : std::format("its element flag +0x75 = {} ({})", excl,
                                                                excl ? "EXCLUSIVE: the manager's check below applies"
                                                                     : "not exclusive: the condition object decides");
            const std::string lk = (cur & stp::kStageMask)
                                       ? std::format("{} there (lookup read {}, dead {})", live ? "LIVE" : "not live",
                                                     v[8] & 1 ? "ok" : "failed", (v[8] >> 2) & 1 ? "yes" : "no")
                                       : std::string("no current stage");
            return std::format(
                "ShopProbe [branch why] {} T{} | {}'s branch at {}: sequencer 0x{:X} (scene +0x30 0x{:X}), target node "
                "0x{:X}: {} | the scene's manager +0x28 0x{:X} ({}; {} the B1 manager 0x{:X}): current stage +0xA0 0x{:X} "
                "/ +0x90 0x{:X} -> 0x{:X}, {} -> {} | scene type +0x68 {}, group +0x78 "
                "0x{:08X} (the current stage's 0x{:08X}) | the node's condition object: {} (vt exe+0x{:X}; +8 exe+0x{:X}, "
                "+0x10 exe+0x{:X})",
                when, e.tid, (v[0] >> 8) & 1 ? "USER 2" : "the host / another", site == 0 ? "2C0BFD7" : "2C0C03A", v[1],
                v[2], node_id, ex, v[3], v[11] ? rtti_name(v[11]) : std::string("?"), v[3] == v[4] ? "=" : "!=", v[4],
                v[6], v[5], cur, lk,
                shp::excl_name(verdict), v[7] & 0xFF, static_cast<uint32_t>(v[7] >> 32),
                static_cast<uint32_t>(v[8] >> 32), v[9] ? rtti_name(v[9]) : std::string("none"), v[9],
                v[10] & 0xFFFFFFFF, v[10] >> 32);
        }
        case kEBranchNames:
            return std::format("ShopProbe [branch why] {} T{} | names: the manager's current stage 0x{:X} '{}' | this "
                               "scene 0x{:X} '{}'",
                               when, e.tid, v[10], name_of(v, 5), v[11], name_of(v + 5, 5));
        case kEFocus: {
            const auto what = static_cast<uint32_t>(v[0] & 0xFF);
            const bool u2req = (v[0] >> 8) & 1, any = (v[0] >> 9) & 1, u2mgr = (v[0] >> 10) & 1;
            const auto owner48 = static_cast<uint32_t>(v[8]), owner98 = static_cast<uint32_t>(v[8] >> 32),
                       u2key = static_cast<uint32_t>(v[9] >> 32), ret = static_cast<uint32_t>(v[9]);
            const uint64_t cur = stp::current_of(v[4], v[2]);
            const bool u2own = ((v[0] >> 11) & 1) || (u2key && (owner98 == u2key || owner48 == u2key));
            const std::string scene =
                v[6] ? std::format("scene 0x{:X} ('{}', owner +0x98 0x{:08X} / [+0x48]+0x60 0x{:08X}{})", v[7],
                                   name_of(v + 10, 2), owner98, owner48, u2own ? ": USER 2's" : "")
                     : std::string("scene -");
            return std::format(
                "ShopProbe [focus] {} T{} | {} in manager 0x{:X}{} | before: +0xA0 0x{:X} +0x90 0x{:X} +0x98 0x{:X} (focus "
                "0x{:X}){} | {} | {}{}",
                when, e.tid,
                what == 0 ? std::format("SET to 0x{:X} (a flagged branch)", v[5])
                : what == 1 ? std::string("UNDO (+0xA0 = 0, +0x90 = +0x98)")
                            : std::string("a stage's END (clears the focus if it was the focus)"),
                v[1], u2mgr ? " (USER 2's own manager)" : "", v[4], v[2], v[3], cur,
                what == 0 && cur && (cur & stp::kStageMask) && cur != v[5] ? " TAKEN FROM ANOTHER STAGE" : "", scene,
                u2req ? "inside USER 2's branch request" : any ? "inside a branch request (not user 2's)"
                                                               : "outside any branch request (a tick or another request)",
                ret ? std::format(" | from exe+0x{:X}", ret) : std::string());
        }
        case kEOwnScene: {
            const uint64_t f = v[3];
            const auto where = [](bool ok, bool found, bool dead) {
                return !ok ? "unreadable" : found ? "HAS it" : dead ? "has it ended" : "does not have it";
            };
            const bool in_host = (f >> 1) & 1, in_own = (f >> 4) & 1;
            return std::format(
                "ShopProbe [own scene] {} T{} | the joined player's own scene 0x{:X} ('{}', its character 0x{:08X}) at "
                "its PrepareSequencerAck: the host's manager 0x{:X} {}, its own manager 0x{:X} {} -> {}",
                when, e.tid, v[0], name_of(v + 5, 5), static_cast<uint32_t>(f >> 32), v[1],
                where(f & 1, in_host, (f >> 2) & 1), v[2], where((f >> 3) & 1, in_own, (f >> 5) & 1),
                in_host && !in_own   ? "in the HOST's manager: the per-player focus slot is for it"
                : in_own && !in_host ? "in ITS OWN manager: its focus is apart already"
                : in_own && in_host  ? "in both?"
                                     : "in neither yet");
        }
        default:
            return std::format("ShopProbe: event {}?", e.kind);
    }
}

std::string by_who(const std::atomic<uint64_t>* a) {
    return std::format("user 2 {} / host {} / unknown {}", a[static_cast<size_t>(Who::User2)].load(),
                       a[static_cast<size_t>(Who::Host)].load(), a[static_cast<size_t>(Who::Unknown)].load());
}

void twin_summary() {
    std::string verdicts;
    for (size_t k = 0; k < static_cast<size_t>(shp::TwinVerdict::Count); ++k) {
        if (const uint64_t n = g_twin_verdicts[k].load()) {
            verdicts += std::format(" {} {};", shp::twin_verdict_name(static_cast<shp::TwinVerdict>(k)), n);
        }
    }
    log::info("FuncnpcTwin summary (funcnpc_twin={}): P {} | user 2 character 0x{:08X} manager 0x{:X} | verdicts:{} | "
              "host scenes with a twin decided {}",
              g_twin_asked_on.load() ? "1, held as probe" : "probe", g_p_on.load() ? "in" : "NOT in", g_u2_key.load(),
              g_u2_mgr.load(), verdicts.empty() ? " none" : verdicts, g_twinned.size());
}

void summary() {
    if (g_twin.load()) twin_summary();
    if (!g_on.load()) return;
    shp::KeyRow rows[16];
    const size_t n = g_keys.rows(rows, 16);
    std::string keys;
    for (size_t i = 0; i < n; ++i) {
        const shp::KeyRow& r = rows[i];
        keys += std::format(" 0x{:08X} (user 0x{:08X}): updates {}, gate {}, owner {}, NOT owner {};", r.key, r.user_key,
                            r.entries, r.gate, r.owner, r.not_owner);
    }
    const auto res = [](size_t w) {
        return std::format("0:{} 1:{} 2:{} ?:{}", g_srv_result[w][0].load(), g_srv_result[w][1].load(),
                           g_srv_result[w][2].load(), g_srv_result[w][3].load());
    };
    log::info("ShopProbe summary: hooks E {} G {} P {} C {} S {}/{}/{} | user 2 = user 0x{:08X} character 0x{:08X} | "
              "stage update per character:{} | stage elements prepared {} (failed {}), cd_seq_funcnpc_* {} (owner not "
              "the manager's character {}; ids kept {}) | 2671 on the server: {} (refused: {}) | server 0x2126920 calls: "
              "{} (lookup found: {}); results user 2 [{}] host [{}] | user 2 Req 2671 {} 2720 {} 2436 {} (funcnpc {}, "
              "dropped by R1 {}) 2591 {} (funcnpc {}, dropped {}) | to user 2: 2663 {} 3000 {} 2916 {} 2525 {} 2981 {} "
              "(funcnpc {}) 2524 {} (funcnpc {}) | limited {} lost {}",
              g_e_on.load() ? "in" : "NOT in", g_g_on.load() ? "in" : "NOT in", g_p_on.load() ? "in" : "NOT in",
              g_c_on.load() ? "in" : "NOT in", g_sa_on.load() ? "in" : "NOT in", g_sr_on.load() ? "in" : "NOT in",
              g_sf_on.load() ? "in" : "NOT in", server_user2_user_key(), server_user2_player_key(),
              keys.empty() ? " none" : keys, g_prep.load(), g_prep_fail.load(), g_prep_funcnpc.load(),
              g_prep_funcnpc_not_owner.load(), g_funcnpc.size(), by_who(g_2671), by_who(g_2671_fail), by_who(g_srv),
              by_who(g_srv_found), res(static_cast<size_t>(Who::User2)), res(static_cast<size_t>(Who::Host)),
              g_req[0].load(), g_req[1].load(), g_req[2].load(), g_req_funcnpc[2].load(), g_req_filtered[2].load(),
              g_req[3].load(), g_req_funcnpc[3].load(), g_req_filtered[3].load(), g_out[0].load(), g_out[1].load(),
              g_out[2].load(), g_out[3].load(), g_out[4].load(), g_out_funcnpc[4].load(), g_out[5].load(),
              g_out_funcnpc[5].load(), g_limited.load(), g_lost.load());
}

// BUG 2 (2026-11-25, scratchpad errname73.py / errname73b.py on the 3732 exe): the joined player's talk is refused
// with 0x58C8345D while the host talks. The two preempt errors' globals (filled at start from their names): the
// requester already holding a preemption ([comp+0xF0], 0x2359089 / 0xC958F21) and the NPC's reserve 0x235D030
// failing (0x235BDB9 / 0x2A33ED0). Their values, once, name which one it is.
constexpr uintptr_t kErrAlreadyPreempted = 0x71D6974, kErrAlreadyPreempting = 0x71D6978;
void preempt_errors_once() {
    static bool said = false;
    if (said || !g_base) return;
    const uint32_t ed = rdv<uint32_t>(g_base + kErrAlreadyPreempted), ing = rdv<uint32_t>(g_base + kErrAlreadyPreempting);
    if (!ed && !ing) return;  // not filled yet: the next minute
    said = true;
    log::info("ShopProbe: the game's preempt errors: eErrNoAlreadyPreemptedActor 0x{:08X} (the NPC's reserve failed), "
              "eErrNoAlreadyPreemptingActor 0x{:08X} (the requester already holds one); the friend's refusals while the "
              "host talks carry 0x58C8345D",
              ed, ing);
    // cs127c: 0x58C8345D is neither. Every error name's global sits in 0x71B0000..0x71E0000 (scratchpad
    // errtable73.csv, the registrar 0x164E1D0's globals): the ones holding a refusal code seen in a log, named offline
    // from that table. 0x58C8345D: the friend's talk refusal (BUG 2: eErrNoInvalidStageChartInteractionCondition).
    // 0x7B1E861C (Brycecco + Arcticwolf 25 Nov, a03a63b9): the host's pickup of Shia's pendant (0xB020093B, interaction
    // 0xF4244, an object bound to the host's story scene 0x352) refused 20+ times, its pickup animation stuck; the
    // joined player's interaction 0x275F refused with it too.
    constexpr uint32_t kCodes[] = {0x58C8345Du, 0x7B1E861Cu};
    std::string hits[std::size(kCodes)];
    int n[std::size(kCodes)] = {};
    for (uintptr_t rva = 0x71B0000; rva < 0x71E0000; rva += 0x1000) {
        uint32_t page[0x400];
        if (!game::fast_read(g_base + rva, page, sizeof page)) continue;
        for (size_t i = 0; i < std::size(page); ++i) {
            for (size_t c = 0; c < std::size(kCodes); ++c) {
                if (page[i] == kCodes[c] && n[c] < 8) {
                    hits[c] += std::format("{}0x{:X}", n[c] ? ", " : "", rva + i * 4);
                    ++n[c];
                }
            }
        }
    }
    for (size_t c = 0; c < std::size(kCodes); ++c)
        log::info("ShopProbe: error globals holding 0x{:08X} (a refusal seen in a log): {}", kCodes[c],
                  hits[c].empty() ? "none" : hits[c]);
}

void branch_summary() {
    preempt_errors_once();
    if (!g_b2_on.load()) return;
    log::info("ShopProbe branch summary: the StageBranchStateReq handler's 0x2B56870: user 2 {} (refused {}), the host "
              "/ others {} (refused {}), without B1 {} | user 2's branch sequencers seen {} | the step's refusals at "
              "2C0BFD7 user 2 {} / others {}, at 2C0C03A (the condition) user 2 {} / others {}",
              g_branch[0].load(), g_branch_fail[0].load(), g_branch[1].load(), g_branch_fail[1].load(),
              g_branch_unpaired.load(), g_u2_branch_seqs.size(), g_bf[0][1].load(), g_bf[0][0].load(), g_bf[1][1].load(),
              g_bf[1][0].load());
    if (g_fs_on.load() || g_fu_on.load() || g_fd_on.load()) {
        log::info("ShopProbe focus summary: requests handed to ServerNet::OnRecvPacket {} (user 2's {}) | focus set {} "
                  "(in user 2's requests {}), undo {} ({}), stage ends {} ({}) | user 2's own scenes at their "
                  "PrepareSequencerAck: in the host's manager {}, in its own {}, in both {}, in neither {}",
                  g_recv_all.load(), g_recv_u2.load(), g_focus_n[0][0].load() + g_focus_n[0][1].load(),
                  g_focus_n[0][1].load(), g_focus_n[1][0].load() + g_focus_n[1][1].load(), g_focus_n[1][1].load(),
                  g_focus_n[2][0].load() + g_focus_n[2][1].load(), g_focus_n[2][1].load(), g_own_where[0].load(),
                  g_own_where[1].load(), g_own_where[2].load(), g_own_where[3].load());
    }
    if (g_slot_on.load()) {
        // g_slots' counters are the server thread's plain integers, read here as they are (a log line only).
        log::info("ShopProbe per-player focus slot (the logic thread T{}): user 2's branches of its own scenes in the "
                  "host's manager {} (other branches passed by {}), its talks {} (to the NPC of the host's own talk, "
                  "left to the game: {}; the C0 window {}), its stages' setter {} / ends {} / ticks {}, the "
                  "host's stages inside its requests {} | swaps {} failed {} overflow {} unbalanced {} repairs {} missed "
                  "leaves {} (unwound {}, dropped {}), resets for another manager {} | other managers {}, other "
                  "threads {} | the host's manager 0x{:X} "
                  "(character 0x{:08X})",
                  g_logic_tid.load(), g_slot_u2[0].load(), g_slot_u2_passed.load(), g_slot_talks.load(),
                  g_slot_talks_left.load(), g_c0_on.load() ? "hooked" : "NOT hooked", g_slot_u2[1].load(),
                  g_slot_u2[2].load(),
                  g_slot_u2[3].load(),
                  g_slot_host_in_u2.load(), g_slots.swaps(), g_slots.failed(), g_slots.overflow(), g_slots.unbalanced(),
                  g_slots.repairs(), g_slots.missed(), g_slot_unwinds.load(), g_slot_dropped.load(),
                  g_slot_resets.load(), g_slot_other.load(),
                  g_slot_other_thread.load(), g_host_mgr.load(), g_host_key.load());
    }
}

void drain() {
    p::Event e;
    static uint64_t focus_lines = 0;  // the focus probe's own budget: it never takes the other lines' kMaxLines
    while (g_ring.pop(e)) {
        if (e.kind >= kKindCount) continue;
        if (e.kind == kEFocus) {
            if (focus_lines++ < 1500) log::info("{}", line_of(e));
            continue;
        }
        if (g_lines.fetch_add(1) < kMaxLines) log::info("{}", line_of(e));
    }
}

// ---------------------------------------------------------------- install

bool bytes_at(uintptr_t rva, std::span<const uint8_t> expected) {
    std::vector<uint8_t> actual(expected.size());
    return game::safe_read(g_base + rva, actual.data(), actual.size()) &&
           std::equal(actual.begin(), actual.end(), expected.begin());
}

bool context_ok(std::span<const gad::Section> secs, uintptr_t rva, std::span<const uint8_t> bytes, const char* what) {
    if (gad::in_real_code(secs, static_cast<uint32_t>(rva), static_cast<uint32_t>(bytes.size())) && bytes_at(rva, bytes)) {
        return true;
    }
    log::error("ShopProbe: {} (exe+0x{:X}, {} bytes) is not build 2976's .rsrc code; REFUSED", what, rva, bytes.size());
    return false;
}

// user2_focus_slot=1: the three inline hooks; all or none (the request window FR1 / FR2 must be in too). Review
// 2026-11-24 #2 / #3: each is prepared disabled through game::prepare_inline, which must relocate exactly the verified
// 5-byte first instruction (a 14-byte fallback would move unverified bytes: refused); only when all three are
// prepared are they moved into place and enabled; an enabled hook is never destroyed (a server thread may be inside
// its trampoline): on a failure the enabled ones are disabled and kept. Returns whether 0x21CB970 got its hook.
bool install_focus_slot(std::span<const gad::Section> secs) {
    struct One {
        uintptr_t rva;
        std::span<const uint8_t> head;
        void* detour;
        SafetyHookInline* slot;
        const char* what;
    };
    const One ones[] = {
        {kFSetFn, kFSetHead, reinterpret_cast<void*>(&fset_detour), &g_fset_hook, "the focus setter 0x2271150"},
        {kFTickFn, kFTickHead, reinterpret_cast<void*>(&ftick_detour), &g_ftick_hook, "the stage processor's tick 0x226DA80"},
        {kFEndFn, kFEndHead, reinterpret_cast<void*>(&fend_detour), &g_fend_hook, "a stage's end 0x21CB970"},
    };
    const bool fr = g_fr1_on.load() && g_fr2_on.load();
    std::vector<std::pair<SafetyHookInline*, SafetyHookInline>> ready;  // prepared, disabled
    ready.reserve(std::size(ones));
    const char* failed = fr ? nullptr : "the request window FR1 / FR2";
    for (const One& o : ones) {
        if (failed) break;
        if (!context_ok(secs, o.rva, o.head, o.what)) {
            failed = o.what;
            break;
        }
        const auto whole = [&o](const std::vector<uint8_t>& b) {
            return b.size() == 5 && std::memcmp(b.data(), o.head.data(), 5) == 0;
        };
        auto h = game::prepare_inline(g_base + o.rva, o.detour, whole, "ShopProbe", o.what);
        if (!h) {
            log::error("ShopProbe: {} (exe+0x{:X}) could not be prepared ({}); nothing written", o.what, o.rva,
                       game::inline_error_text(h.error()));
            failed = o.what;
            break;
        }
        if (!whole(h->original_bytes())) {
            log::error("ShopProbe: {} (exe+0x{:X}) would patch {} bytes, not its verified 5-byte first instruction; "
                       "nothing written",
                       o.what, o.rva, h->original_bytes().size());
            failed = o.what;
            break;
        }
        ready.emplace_back(o.slot, std::move(*h));
    }
    if (!failed) {
        for (auto& r : ready) *r.first = std::move(r.second);
        std::vector<SafetyHookInline*> enabled;
        for (auto& r : ready) {
            if (!r.first->enable()) {
                for (SafetyHookInline* e : enabled) (void)e->disable();
                failed = "enabling the hooks";
                break;
            }
            enabled.push_back(r.first);
        }
    }
    if (!failed) {
        g_fd_on.store(true, std::memory_order_release);  // the stage-end detour logs the focus ends
        g_slot_on.store(true, std::memory_order_release);
        log::info("ShopProbe: the per-player focus slot (user2_focus_slot=1) is ON, on the server's logic thread (learned "
                  "at the first branch): user 2's branches of its own scenes in the host's manager, its talks' check "
                  "(C0 {}; not to the NPC of the host's own talk) and its own stages' focus work run with its own +0x90 "
                  "/ +0x98 / +0xA0 in the host's manager",
                  g_c0_on.load() ? "hooked" : "NOT hooked");
        return true;
    }
    log::error("ShopProbe: the per-player focus slot (user2_focus_slot=1) stays OFF ({} failed)", failed);
    return static_cast<bool>(g_fend_hook);  // moved into place (now disabled): 0x21CB970 takes no mid hook then
}

void install(std::span<const gad::Section> secs) {
    const auto mid = [&](uintptr_t rva, uint32_t patch, std::span<const uint8_t> bytes, safetyhook::MidHookFn fn,
                         SafetyHookMid& out, std::atomic<bool>& on, const char* what) {
        if (p8_install_mid(g_base, secs, rva, patch, bytes, fn, out, "ShopProbe", what)) on.store(true, std::memory_order_release);
    };
    if (context_ok(secs, kPCtx, kPCtxBytes, "0x288A970's call 0x287C200")) {
        mid(kP, kPPatch, kPBytes, on_p, g_p_hook, g_p_on, "P, after the stage element's 0x287C200");
    }
    if (!g_on.load()) {  // funcnpc_twin alone: only P
        log::info("FuncnpcTwin (funcnpc_twin={}): P 0x288AAEF {} (shop_probe=0: no other point)",
                  g_twin_asked_on.load() ? "1, held as probe" : "probe", g_p_on.load() ? "hooked" : "REFUSED");
        return;
    }
    mid(kE, kEPatch, kEBytes, on_e, g_e_hook, g_e_on, "E, the stage update's entry 0x288B7A0");
    if (context_ok(secs, kGCtx, kGCtxBytes, "the owner gate's user key 0x288B867")) {
        mid(kG, kGPatch, kGBytes, on_g, g_g_hook, g_g_on, "G, the owner gate 0x288B87A");
    }
    if (context_ok(secs, kCCtx, kCCtxBytes, "the 2671 handler's call 0x2B36860")) {
        mid(kC, kCPatch, kCBytes, on_c, g_c_hook, g_c_on, "C, the 2671's result 0x29E9282");
        // BUG 2's talk window (the per-player slot, 2026-11-25): C0 right before the check, installed after C (C's
        // context check read these bytes first); without C no window (it would never close).
        if (g_c_on.load() && g_slot_wanted.load()) {
            mid(kC0, kC0Patch, kC0Bytes, on_c0, g_c0_hook, g_c0_on,
                "C0, before the 2642 handler's check 0x2E3C180 (the talk window)");
        }
    }
    if (context_ok(secs, kBCtx, kBCtxBytes, "the StageBranchStateReq handler's vf48 0x2D55B36") &&
        context_ok(secs, kBCallCtx, kBCallCtxBytes, "the handler's call 0x2B56870")) {
        mid(kB1, kB1Patch, kB1Bytes, on_b1, g_b1_hook, g_b1_on, "B1, before the branch 0x2B56870");
        if (g_b1_on.load()) mid(kB2, kB2Patch, kB2Bytes, on_b2, g_b2_hook, g_b2_on, "B2, the branch's result");
    }
    // Co-op+ 2026-11-24: why a branch is refused (BF1 / BF2; read only, needs B1 for whose branch it is).
    if (g_b1_on.load() && context_ok(secs, kBF1Ctx, kBF1CtxBytes, "the branch step's error load 0x2C0BFD7") &&
        context_ok(secs, kBF2Ctx, kBF2CtxBytes, "the branch step's error load 0x2C0C03A")) {
        mid(kBF1, kBFPatch, kBFBytes, on_bf1, g_bf1_hook, g_bf1_on, "BF1, the branch step's refusal (a)");
        mid(kBF2, kBFPatch, kBFBytes, on_bf2, g_bf2_hook, g_bf2_on, "BF2, the branch step's refusal (the condition)");
    }
    log::info("ShopProbe: BF 0x2C0BFDD/0x2C0C040 (why the branch step refuses) {}/{}", g_bf1_on.load() ? "hooked" : "off",
              g_bf2_on.load() ? "hooked" : "off");
    // Co-op+ 2026-11-24 (late): the focus slot's writers and whose request is running (read only).
    mid(kFR1, kFR1Patch, kFR1Bytes, on_fr1, g_fr1_hook, g_fr1_on, "FR1, before ServerNet::OnRecvPacket's call");
    if (g_fr1_on.load()) {
        mid(kFR2, kFR2Patch, kFR2Bytes, on_fr2, g_fr2_hook, g_fr2_on, "FR2, after it");
        if (!g_fr2_on.load()) g_fr1_on.store(false, std::memory_order_release);  // a window that never closes: none
    }
    mid(kFS, kFSPatch, kFSBytes, on_fs, g_fs_hook, g_fs_on, "FS, the focus setter's store 0x2271252");
    mid(kFU, kFUPatch, kFUBytes, on_fu, g_fu_hook, g_fu_on, "FU, the focus undo 0x21CAF10");
    // With the per-player slot, 0x21CB970's entry takes an inline hook instead (its detour logs the same events);
    // without it, or when that hook could not be placed, the mid hook.
    const bool end_hooked = g_slot_wanted.load() && install_focus_slot(secs);
    if (!end_hooked) {
        mid(kFD, kFDPatch, kFDBytes, on_fd, g_fd_hook, g_fd_on, "FD, the focus clear at a stage's end 0x21CB970");
    }
    log::info("ShopProbe: focus probe (read only): FR 0x2A10B89/0x2A10B9A {}/{} | FS 0x2271252 {} | FU 0x21CAF10 {} | FD "
              "0x21CB970 {}",
              g_fr1_on.load() ? "hooked" : "off", g_fr2_on.load() ? "hooked" : "off", g_fs_on.load() ? "hooked" : "off",
              g_fu_on.load() ? "hooked" : "off", g_fd_on.load() ? "hooked" : "off");
    log::info("ShopProbe: B 0x2D55B56/0x2D55B6A (the StageBranchStateReq handler's result) {}/{}",
              g_b1_on.load() ? "hooked" : "REFUSED", g_b2_on.load() ? "hooked" : "REFUSED");
    if (context_ok(secs, isl::kCtxLookup, isl::kCtxLookupBytes, "the entry check's lookup call 0x2126A5C") &&
        context_ok(secs, isl::kCtxA, isl::kCtxABytes, "the entry check's vf48 call 0x2126A14")) {
        mid(isl::kPointR, isl::kPatchR, isl::kBytesR, on_sr, g_sr_hook, g_sr_on, "S/R, after the lookup 0x2126AAB");
        mid(isl::kPointF, isl::kPatchF, isl::kBytesF, on_sf, g_sf_hook, g_sf_on, "S/F, the result 0x2126D11");
        mid(isl::kPointA, isl::kPatchA, isl::kBytesA, on_sa, g_sa_hook, g_sa_on, "S/A, the manager 0x2126A1F");
    }
    log::info("ShopProbe (shop_probe=1, read only): E 0x288B7A0 {} | G 0x288B87A {} | P 0x288AAEF {} | C 0x29E9282 {} | "
              "S 0x2126A1F/0x2126AAB/0x2126D11 {}/{}/{} | taps: user 2's 2671/2720/2436/2591 (with observer_seq_copy's "
              "R1 verdict{}) and the 2663/3000/2916/2525/2981/2524 queued for user 2",
              g_e_on.load() ? "hooked" : "REFUSED", g_g_on.load() ? "hooked" : "REFUSED",
              g_p_on.load() ? "hooked" : "REFUSED", g_c_on.load() ? "hooked" : "REFUSED",
              g_sa_on.load() ? "hooked" : "REFUSED", g_sr_on.load() ? "hooked" : "REFUSED",
              g_sf_on.load() ? "hooked" : "REFUSED",
              observer_seq_copy_wanted() ? "" : "; observer_seq_copy is off: nothing is filtered");
}

// The module's thread, every second (S2 probe): user 2's character and manager, validated, for the hook P.
void refresh_user2() {
    uintptr_t user = 0, chr = 0, comps = 0, seq = 0, mgr = 0, vt = 0;
    uint32_t key = 0;
    const uint32_t want = server_user2_player_key();
    if (want && server_user2(user) && user && game::safe_read(user + kUser_Main, &chr, sizeof chr) && chr &&
        game::safe_read(chr + kActor_Key, &key, sizeof key) && key == want &&
        game::safe_read(chr + kActor_Comps, &comps, sizeof comps) && comps &&
        game::safe_read(comps + kComps_Seq, &seq, sizeof seq) && seq &&
        game::safe_read(seq + kSeqComp_Mgr, &mgr, sizeof mgr) && mgr && game::safe_read(mgr, &vt, sizeof vt) &&
        vt == g_base + isl::kVtServerStageMgr) {
        g_u2_char.store(chr);
        g_u2_key.store(key);
        g_u2_mgr.store(mgr);
        return;
    }
    g_u2_char.store(0);
    g_u2_key.store(0);
    g_u2_mgr.store(0);
}

// The module's thread, every second (user2_focus_slot): the host's ServerSequencerStageManager (the chain
// refresh_user2 follows, from the host's user), and user 2's comings and goings (its own triple starts empty).
void refresh_host_for_slot() {
    uint32_t key = 0;
    const uintptr_t mgr = host_mgr_live(&key);  // 0 when the chain is broken: the detours then pass everything on
    const uintptr_t was = g_host_mgr.exchange(mgr, std::memory_order_acq_rel);
    if (key) g_host_key.store(key, std::memory_order_relaxed);
    if (was != mgr) {
        if (mgr) g_slot_reset_all.store(true, std::memory_order_release);
        log::info("ShopProbe: the per-player focus slot follows the host's manager 0x{:X} (was 0x{:X})", mgr, was);
    }
    static bool u2_was = false;
    const bool u2 = server_user2_present();
    if (u2 != u2_was) {
        g_slot_reset_u2.store(true, std::memory_order_release);
        u2_was = u2;
    }
}

DWORD WINAPI probe_thread(LPVOID) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::vector<gad::Section> secs;
    if (g_base != off::kImageBase || !game::safe_read(g_base, &dos, sizeof dos) ||
        !game::safe_read(g_base + dos.e_lfanew, &nt, sizeof nt) || nt.FileHeader.TimeDateStamp != off::kPeTimestamp ||
        !p8_exe_sections(g_base, secs)) {
        log::error("ShopProbe: unsupported game build or unreadable sections; nothing installed (taps still count)");
    } else {
        for (int i = 0; i < 1200; ++i) {  // WorldSystem (up to 10 minutes): the game's code is in place
            uintptr_t world = 0;
            if (game::safe_read(g_base + off::kWorldSystemSlot, &world, sizeof world) && world) break;
            Sleep(500);
        }
        try {
            install(secs);
        } catch (...) {
            log::error("ShopProbe: install failed (exception); hooks may be partial");
        }
    }
    for (uint64_t tick = 1;; ++tick) {
        Sleep(1000);
        try {
            if (g_twin.load() || g_fs_on.load()) refresh_user2();  // the [focus] lines name user 2's own manager too
            if (g_slot_wanted.load()) refresh_host_for_slot();
            drain();
            if (tick % 60 == 0) {
                summary();
                branch_summary();
            }
        } catch (...) {
        }
    }
}

}  // namespace

void shop_probe_start(bool on, const std::string& twin_mode) {
    const shp::TwinKey tk = shp::parse_twin(twin_mode);
    if (tk.bad) log::warn("FuncnpcTwin: funcnpc_twin={} is not 0, probe or 1: off", twin_mode);
    if (tk.asked_on) {
        log::warn("FuncnpcTwin: funcnpc_twin=1 is NOT applied in this build: held as probe (no call to 0x287C200 for "
                  "user 2's manager until T2 and the routing of user 2's scene Reqs are settled; "
                  "p8-impl-tenderos-s2.md)");
    }
    g_twin.store(static_cast<uint8_t>(tk.mode));
    g_twin_asked_on.store(tk.asked_on);
    if (!on && g_slot_wanted.load()) {
        log::warn("ShopProbe: user2_focus_slot=1 needs shop_probe=1 (its hooks are this module's): the per-player focus "
                  "slot stays off");
    }
    if (!on && tk.mode == shp::TwinMode::Off) return;
    g_on.store(on);
    if (g_started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, probe_thread, nullptr, 0, nullptr));
}

void shop_probe_set_focus_slot(bool on) {
    g_slot_wanted.store(on);
    if (on) log::info("ShopProbe: user2_focus_slot=1 asked (the per-player focus slot; needs shop_probe=1)");
}

bool shop_probe_active() noexcept { return g_on.load(std::memory_order_relaxed); }

uint32_t shop_probe_logic_thread() noexcept { return g_logic_tid.load(std::memory_order_acquire); }

bool shop_probe_gate_live() noexcept { return g_g_on.load(std::memory_order_acquire); }

bool shop_probe_route_points_live() noexcept {
    return g_sa_on.load(std::memory_order_acquire) && g_p_on.load(std::memory_order_acquire);
}

void shop_probe_on_user2_req(uint16_t opcode, std::span<const uint8_t> payload, bool filtered) noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return;
    size_t i = 0;
    switch (opcode) {
        case shp::kOpPreemptionReq: i = 0; break;
        case shp::kOpDoStepReq: i = 1; break;
        case shp::kOpBranchReq: i = 2; break;
        case shp::kOpChartEvent: i = 3; break;
        default: return;
    }
    g_req[i].fetch_add(1, std::memory_order_relaxed);
    if (filtered) g_req_filtered[i].fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    bool funcnpc = false;
    if (i < 2) {
        const shp::Interaction r = shp::parse_interaction(payload);
        e.v[1] = r.requester;
        e.v[2] = r.npc;
        e.v[3] = r.interaction;
    } else {
        const shp::SeqReq r = shp::parse_seq_req(payload);
        if (i == 2) g_u2_branch_seqs.add(r.seq);  // B: the handler's result is told as user 2's
        funcnpc = g_funcnpc.has(r.seq);
        if (funcnpc) g_req_funcnpc[i].fetch_add(1, std::memory_order_relaxed);
        // 2591s are many (stage chart events): only the funcnpc ones and the first 8 others are logged.
        if (i == 3 && !funcnpc && !g_book.take(book_key(0x2591, 0x50), 8)) return;
        e.v[1] = r.seq;
        e.v[2] = r.after;
    }
    e.v[0] = opcode | static_cast<uint64_t>(filtered) << 16 | static_cast<uint64_t>(funcnpc) << 17;
    emit(kEReq, e);
}

void shop_probe_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool to_user2, bool pushed) noexcept {
    if (!g_on.load(std::memory_order_relaxed) || !to_user2) return;
    if (opcode == shp::kOpPrepareAck) {  // a scene of user 2's own: the per-player focus slot's owner test
        const shp::PrepareOwner po = shp::parse_prepare_owner(payload);
        const uint32_t u2 = server_user2_player_key();
        if (!po.ok || !u2 || po.owner != u2 || !po.id) return;
        // 2026-11-25: which manager has it (the slot is for the ones in the host's), once per id (a held Ack comes
        // here again when it is released).
        if (g_u2_own.has(po.id)) return;
        g_u2_own.add(po.id);
        const uintptr_t hm = host_mgr_live(), um = g_u2_mgr.load(std::memory_order_relaxed);
        const isl::Lookup lh = hm ? isl::find_stage(SehRd{}, hm, po.id) : isl::Lookup{};
        const isl::Lookup lu = um ? isl::find_stage(SehRd{}, um, po.id) : isl::Lookup{};
        g_own_where[lh.found && !lu.found ? 0 : lu.found && !lh.found ? 1 : lh.found ? 2 : 3].fetch_add(
            1, std::memory_order_relaxed);
        p::Event e;
        e.v[0] = po.id;
        e.v[1] = hm;
        e.v[2] = um;
        e.v[3] = static_cast<uint64_t>(lh.read_ok) | static_cast<uint64_t>(lh.found) << 1 |
                 static_cast<uint64_t>(lh.dead) << 2 | static_cast<uint64_t>(lu.read_ok) << 3 |
                 static_cast<uint64_t>(lu.found) << 4 | static_cast<uint64_t>(lu.dead) << 5 |
                 static_cast<uint64_t>(po.owner) << 32;
        put_stage_name(lh.found ? lh.inst : lu.found ? lu.inst : 0, e.v, 5);
        emit(kEOwnScene, e);
        return;
    }
    size_t i = 0;
    switch (opcode) {
        case shp::kOpPreemptionOk: i = 0; break;
        case shp::kOpPreemptionFail: i = 1; break;
        case shp::kOpDoStepOk: i = 2; break;
        case shp::kOpDoInteraction: i = 3; break;
        case shp::kOpBranchAck: i = 4; break;
        case shp::kOpOnBranchEvent: i = 5; break;
        default: return;
    }
    g_out[i].fetch_add(1, std::memory_order_relaxed);
    bool funcnpc = false;
    if (i >= 4) {
        // 2981: u8 | u32 actor | u64 seq ...; 2524: the u64 seq at the same place [E] (guest_seq_route reads both so).
        const shp::BranchAck b = shp::parse_branch_ack(payload);
        funcnpc = b.ok && g_funcnpc.has(b.seq);
        if (!funcnpc) return;  // only the shop scenes' branches
        g_out_funcnpc[i].fetch_add(1, std::memory_order_relaxed);
    }
    p::Event e;
    e.v[0] = opcode | static_cast<uint64_t>(pushed) << 16 | static_cast<uint64_t>(funcnpc) << 17;
    for (size_t k = 0; k < payload.size() && k < 24; ++k) e.v[1 + k / 8] |= static_cast<uint64_t>(payload[k]) << (8 * (k % 8));
    e.v[4] = payload.size();
    emit(kEOut, e);
}

}  // namespace cdmp::session2
