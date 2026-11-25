# Reading the host's server-stall lines (2026-11-25, after Borque's endless loading screen)

The host's native log (cdmp.log) says when the server stops taking the joined player's requests, and from this build
on, what it is doing meanwhile. Game 1.0.0.3732 addresses.

## The lines
- `Session2 inject: the server has not taken our buffers for N ms (epoch E: handed H, seen S, Q more held in our
  queue; ...) | server threads 2: T<a> [PARK-ASKED] [PARKED] busy|idle(ServerRunThread) T<b> ... | ServerRunThread's
  last item: user 2's|the host's <op> entered N ms ago (M items in all)`
  - Written at 1 s, then every 5 s while it lasts; `the server takes our buffers again after N ms` ends it.
  - "handed" grows while "seen" stays: the joined player's requests wait in the server's input queue (Q0).
  - The last item is the request ServerRunThread entered last (its OnRecvPacket entry). If it "entered" about as long
    ago as the stall lasts, the thread is still inside it; the stack sample says where.
  - `PARK-ASKED` on both threads with the second `PARKED`: the game's autosave timer (SaveGameDataAutoTimer 2077) asked
    every server thread to pause and waits for ServerRunThread to finish its item. A bystander, not the cause (it
    clears within seconds once the item ends). Seen in Borque's log and in cs127b.
- `Session2 inject: stall sample (line L of this stall): T<id> (ServerRunThread | the server's logic thread | another
  server thread, busy): rip <where> rsp 0x... (B bytes of stack copied, suspended N us) | returns: <where>, ...`
  - With the stall's first line, each next one for its first ~30 s, then every 30 s; at most 60 a session.
  - `<where>`: `exe+0xRVA` (the game), `cdmp+0xRVA [mod]` (our native base), `<module>+0xRVA` (any other DLL or
    .asi: version.dll, DmmRuntime.asi, ... or the system's: ntdll.dll, KERNELBASE.dll), `0x... [stub]` (private
    executable memory: hook trampolines, ours or another mod's).
  - The returns are the stack values with a call instruction right before them, newest first: the call chain, with
    some stale values mixed in.
  - The logic thread is the server's second registered thread (where the request handlers run); ServerRunThread hands
    requests to it.

## What cs127b showed (the forced 25 s sleep inside our OnRecvPacket hook)
- ServerRunThread: `rip ntdll.dll+0x166A64` (the delay system call) | `ntdll.dll+0xD31A4, KERNELBASE.dll+0x8CCC1`
  (SleepEx), `cdmp+0x32527D [mod]` (our sleep), `cdmp+0x40B1D7 [mod], cdmp+0x5A1571 [mod]` (the mid hook's dispatch),
  `0x7FFDF3741CFD [stub]` (the mid hook's stub), `exe+0x2A10B9A` (the return address of ServerRunThread's call of
  ServerNet::OnRecvPacket 0x2A111F0: inside a request), `exe+0x165B3E6, exe+0x163AEEF` (the thread's start).
- The logic thread first idle (`KERNELBASE.dll+0x1427F` under `exe+0x2A120DD`: waiting for work), then parked by the
  autosave barrier (a sleep under `exe+0x2A12224`).

## How to read a tester's
1. The last item: whose request, which op (3732 numbers; the milestone lines print 2976's for some), how long ago.
2. ServerRunThread's returns: is `exe+0x2A10B9A` there (inside a request) and what is above it: game code only (the
   game's own work), `cdmp+ [mod]` (ours: which hook), another module (another mod's hook or loader), a `[stub]`
   right above game code (someone's hook trampoline).
3. The same frames in every sample of the stall: stuck in one place (a wait: ntdll's wait calls on top) vs changing
   (busy, a long loop).
4. Our own code: name a `cdmp+0xRVA` with the build's PDB (scratchpad/cdmpsym.py: dbghelp on the package's cdmp.asi
   and the cdmp.pdb from its -symbols-KEEP-PRIVATE.zip, never published).

## What the guest's layer does meanwhile (the host tells it: coop_lobby kMsgServerBusy / kMsgGuestLoad)
- `Teleport: the host says its server has not taken requests for N ms` / `... takes requests again`.
- A far travel not started yet: `Teleport: the host's server has not taken requests for N ms: the far travel waits for
  it (no loading screen meanwhile)`, then `... the far travel waited N s and goes now`; within 30 s of a stall's end
  `...; right after a stall of the server: one request only`.
- The join's trip: `Co-op: joined: the host's server is still finishing our own load: the trip to its side waits for
  it`, then `Co-op: joined: our own load is done on the host's server (N s of waiting)`; a note on screen from 5 s on.
- A travel under way during a stall: `Teleport: far travel under way and the server has not taken requests for N ms:
  no second request and no timeout until it answers`.

## What the forced stall taught about the join's trip (cs127b-d, 25 Nov)
- cs127b: the trip waited out the stall in the world, then went with the old 8 s resend: the first request did not move
  the friend, the second (a second reload trip on the host) did.
- cs127c: one request only right after the stall: the reload ran to its end on the server, the friend stayed put,
  the hops after 45 s took it there. The request had gone while the friend's OWN move into the host's field (the
  request that stalled) was still completing; its LoadingCompleteReq came later.
- cs127d: the trip waits for the host's word that the friend's own load is done (the host's MovePhaseTracker:
  STABLE). The guest's 10 s freshness rule let it go early (the host still said COMPLETING); now the last word counts.
- The host's tracker in cs127d: COMPLETING from the stalled 2723 (10:08:44) through the stall, WINDOW_B at the stall's
  end (10:09:09, its MoveFieldCompleteAck), STABLE at 10:09:19 (its LoadingCompleteReq / Ack): about 10 s after a
  25 s stall ends, the friend's own load is done.
