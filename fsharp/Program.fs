module Commander

open System
open System.Threading
open HftDemo.Interface

let setParams (shm: HftShm) bid ask maxPos enabled (version: int64) =
    Volatile.Write(&shm.Region.pars.bid_threshold,   bid)
    Volatile.Write(&shm.Region.pars.ask_threshold,   ask)
    Volatile.Write(&shm.Region.pars.max_position,    maxPos)
    Volatile.Write(&shm.Region.pars.trading_enabled, if enabled then 1 else 0)
    Thread.MemoryBarrier()
    Volatile.Write(&shm.Region.pars.strategy_version, nativeint version)

let mutable tail : uint64 = 0UL

let tryReadEvent (shm: HftShm) : HftExecutionEvent voption =
    let head = uint64 (Volatile.Read(&shm.Region.execution_ring.write_head))
    if tail >= head then ValueNone
    else
        let ev = shm.EventAt(int (tail % uint64 HftNative.HFT_RING_CAPACITY))
        tail <- tail + 1UL
        Volatile.Write(&shm.Region.execution_ring.read_tail, unativeint tail)
        ValueSome ev

let sideStr = function
    |  1 -> "BUY "
    | -1 -> "SELL"
    |  s -> sprintf "?%d?" s

let eventStr = function
    | 0 -> "NEW"
    | 1 -> "FILL"
    | 2 -> "REJECT"
    | e -> sprintf "?%d?" e

let ts (ns: nativeint) =
    DateTimeOffset.FromUnixTimeMilliseconds(ns.ToInt64() / 1_000_000L).ToString("HH:mm:ss.fff")

let printEvent (ev: HftExecutionEvent) =
    printfn "[commander] %s  #%-6d  %s  price=%.4f  qty=%.0f  (%s)"
        (ts ev.timestamp_ns)
        (ev.order_id.ToInt64())
        (sideStr ev.side)
        ev.price
        ev.quantity
        (eventStr ev.event_type)

let drainRing (shm: HftShm) =
    let mutable cont = true
    while cont do
        match tryReadEvent shm with
        | ValueSome ev -> printEvent ev
        | ValueNone    -> cont <- false

let runPhase (shm: HftShm) (seconds: float) =
    let deadline = DateTime.UtcNow.AddSeconds(seconds)
    while DateTime.UtcNow < deadline do
        drainRing shm
        Thread.Sleep(50)

[<EntryPoint>]
let main _ =
    use shm = new HftShm()
    printfn "[commander] shared memory attached"

    let mutable v = 0L

    v <- v + 1L
    printfn "[commander] Phase 1 — bid<=99.80  ask>=100.20  TRADING ON  (v%d)" v
    setParams shm 99.80 100.20 5.0 true v
    runPhase shm 5.0

    v <- v + 1L
    printfn "[commander] Phase 2 — KILL SWITCH  (v%d)" v
    setParams shm 99.80 100.20 5.0 false v
    runPhase shm 2.0

    v <- v + 1L
    printfn "[commander] Phase 3 — bid<=99.95  ask>=100.05  TRADING ON  (v%d)" v
    setParams shm 99.95 100.05 5.0 true v
    runPhase shm 5.0

    drainRing shm
    printfn "[commander] done"
    0
