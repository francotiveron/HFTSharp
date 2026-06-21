using System.Runtime.InteropServices;

namespace HftDemo.Interface;

// Unsafe pointer work is confined here.
// F# calls Volatile.Read / Volatile.Write on the refs this class exposes.
public sealed unsafe class HftShm : IDisposable
{
    private HftSharedRegion* _region;
    private bool _disposed;

    public HftShm()
    {
        _region = (HftSharedRegion*)HftNative.hft_shm_init();
        if (_region == null) throw new InvalidOperationException("Failed to attach shared memory.");
    }

    public ref HftSharedRegion Region => ref *_region;

    public Span<HftExecutionEvent> Events => MemoryMarshal.CreateSpan(ref _region->execution_ring.events[0], (int)HftNative.HFT_RING_CAPACITY);

    public void Dispose()
    {
        if (!_disposed && _region != null)
        {
            HftNative.hft_shm_cleanup(_region);
            _region = null;
            _disposed = true;
        }
    }
}
