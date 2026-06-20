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
        if (_region == null)
            throw new InvalidOperationException("Failed to attach shared memory.");
    }

    public ref HftSharedRegion Region => ref *_region;

    // [InlineArray] indexing is a C# 12 feature F# cannot use.
    public ref HftExecutionEvent EventAt(int slot)
        => ref _region->execution_ring.events[slot];

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
