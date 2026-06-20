module Commander

open System
open System.Threading
open HftDemo.Interface

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
let sideStr (side: int) =
    match side with
    |  1 -> "BUY "
    | -1 -> "SELL"
    |  s -> sprintf "?%d?" s

let eventStr (e: int) =
    match e with
    | 0 -> "NEW"
    | 1 -> "FILL"
    | 2 -> "REJECT"
    | e -> sprintf "?%d?" e

let ts (ns: nativeint) =
    let dt = DateTimeOffset.FromUnixTimeMilliseconds(ns.ToInt64() / 1_000_000L)
    dt.ToString("HH:mm:ss.fff")

let printEvent (ev: HftExecutionEvent) =
    printfn "[commander] %s  #%-6d  %s  price=%.4f  qty=%.0f  (%s)"
        (ts ev.timestamp_ns)
        (ev.order_id.ToInt64())
        (sideStr ev.side)
        ev.price
        ev.quantity
        (eventStr ev.event_type)

// ---------------------------------------------------------------------------
// Ring drain — reads all available events
// ---------------------------------------------------------------------------
let drainRing (shm: HftShm) =
    let mutable consumed = 0
    let mutable cont = true
    while cont do
        let ev = shm.TryReadEvent()
        if ev.HasValue then
            printEvent ev.Value
            consumed <- consumed + 1
        else
            cont <- false
    if consumed > 0 then
        printfn "[commander] --- drained %d event(s) ---" consumed

// ---------------------------------------------------------------------------
// Demo scenario
//
//   Phase 1 (0–5 s)  : wide thresholds, trading enabled
//   Phase 2 (5–7 s)  : kill switch ON  — executor halts
//   Phase 3 (7–12 s) : tighter thresholds, trading re-enabled
//   Phase 4 (12 s)   : shutdown
// ---------------------------------------------------------------------------
[<EntryPoint>]
let main _ =
    use shm = new HftShm()
    printfn "[commander] shared memory attached"

    let tickMs  = 50
    let mutable version = 0L

    // Phase 1: wide thresholds, trading on
    version <- version + 1L
    printfn "[commander] Phase 1 — bid<=99.80  ask>=100.20  max_pos=5  TRADING ON  (v%d)" version
    shm.SetParams(99.80, 100.20, 5.0, true, version)

    let phase1End = DateTime.UtcNow.AddSeconds(5.0)
    while DateTime.UtcNow < phase1End do
        drainRing shm
        Thread.Sleep(tickMs)

    // Phase 2: kill switch
    version <- version + 1L
    printfn "\n[commander] Phase 2 — KILL SWITCH ON  (v%d)" version
    shm.SetParams(99.80, 100.20, 5.0, false, version)

    let phase2End = DateTime.UtcNow.AddSeconds(2.0)
    while DateTime.UtcNow < phase2End do
        drainRing shm
        Thread.Sleep(tickMs)

    // Phase 3: tighter thresholds, trading back on
    version <- version + 1L
    printfn "\n[commander] Phase 3 — bid<=99.95  ask>=100.05  max_pos=5  TRADING ON  (v%d)" version
    shm.SetParams(99.95, 100.05, 5.0, true, version)

    let phase3End = DateTime.UtcNow.AddSeconds(5.0)
    while DateTime.UtcNow < phase3End do
        drainRing shm
        Thread.Sleep(tickMs)

    drainRing shm
    printfn "\n[commander] demo complete — shutting down"
    0
