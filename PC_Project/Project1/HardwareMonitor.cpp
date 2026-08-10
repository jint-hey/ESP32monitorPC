#include "HardwareMonitor.h"

#include <utility>

// ============================================================================
// Constructor
// ============================================================================

HardwareMonitor::HardwareMonitor()
{}


// ============================================================================
// Destructor
// ============================================================================

HardwareMonitor::~HardwareMonitor()
{
    /*
        Always stop the worker before destroying:

            CpuMonitor
            GpuMonitor
            MemoryMonitor
            mutexes
            condition_variable
    */

    Stop();
}


// ============================================================================
// Start
// ============================================================================

bool HardwareMonitor::Start(
    unsigned int intervalMs
)
{
    // Sampling interval cannot be zero.
    if (intervalMs == 0)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

        // Already running.
        if (running_)
        {
            return true;
        }

        interval_ =
            std::chrono::milliseconds(
                intervalMs
            );

        stopRequested_ = false;

        intervalChanged_ = false;

        running_ = true;
    }

    /*
        Reset cached data for a fresh monitoring session.

        sequence == 0 means:

            "No new hardware sample is available yet."
    */
    {
        std::lock_guard<std::mutex>
            lock(dataMutex_);

        usage_ = HardwareUsage{};
    }

    sequenceCounter_ = 0;

    try
    {
        workerThread_ =
            std::thread(
                &HardwareMonitor::WorkerLoop,
                this
            );
    }
    catch (...)
    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

        running_ = false;

        stopRequested_ = false;

        intervalChanged_ = false;

        return false;
    }

    return true;
}


// ============================================================================
// Stop
// ============================================================================

void HardwareMonitor::Stop()
{
    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

        if (!running_)
        {
            /*
                Normally there will be no joinable worker here,
                but still leave Stop() harmless when called more
                than once.
            */
            return;
        }

        stopRequested_ = true;
    }

    /*
        Wake the worker immediately.

        Example:

            Worker planned to sleep for 1000 ms
            300 ms have passed
            Stop() is called

        Without condition_variable:

            worker might wait another 700 ms

        With notify_all():

            worker wakes immediately.
    */

    condition_.notify_all();


    /*
        Wait until WorkerLoop() has completely exited.

        This is important because WorkerLoop accesses members
        belonging to this HardwareMonitor object.
    */

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }


    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

        running_ = false;

        stopRequested_ = false;

        intervalChanged_ = false;
    }
}


// ============================================================================
// WorkerLoop
// ============================================================================

void HardwareMonitor::WorkerLoop()
{
    /*
        steady_clock is used instead of system_clock.

        steady_clock cannot jump backwards or forwards when Windows
        adjusts the system clock.

        Therefore it is suitable for:

            timeouts
            sampling intervals
            periodic scheduling
    */

    using Clock =
        std::chrono::steady_clock;


    std::unique_lock<std::mutex>
        lock(controlMutex_);


    /*
        CPU and GPU usage measurements normally depend on the difference
        between two samples.

        CpuMonitor and GpuMonitor establish their initial baseline during
        initialization.

        We therefore wait one full sampling interval before publishing
        HardwareUsage sequence #1.
    */

    auto nextUpdate =
        Clock::now() +
        interval_;


    while (!stopRequested_)
    {
        /*
        =======================================================================
        Wait for one of three conditions
        =======================================================================

        1. nextUpdate time has arrived

        2. Stop() requested shutdown

        3. SetInterval() changed the sampling interval
        */

        const bool interrupted =
            condition_.wait_until(
                lock,
                nextUpdate,
                [this]()
                {
                    return
                        stopRequested_ ||
                        intervalChanged_;
                }
            );


        // --------------------------------------------------------------------
        // Stop requested
        // --------------------------------------------------------------------

        if (stopRequested_)
        {
            break;
        }


        // --------------------------------------------------------------------
        // Sampling interval changed
        // --------------------------------------------------------------------

        if (interrupted &&
            intervalChanged_)
        {
            intervalChanged_ = false;

            /*
                Start a new timing period using the new interval.
            */

            nextUpdate =
                Clock::now() +
                interval_;

            continue;
        }


        /*
        =======================================================================
        Perform hardware sampling
        =======================================================================

        IMPORTANT:

        controlMutex_ is unlocked before hardware queries.

        GPU/PDH queries may take a measurable amount of time.

        We do not want:

            Stop()
            SetInterval()

        to wait for controlMutex_ while hardware collection is running.
        */

        lock.unlock();


        UpdateOnce();


        lock.lock();


        if (stopRequested_)
        {
            break;
        }


        /*
        =======================================================================
        Calculate next absolute sampling point
        =======================================================================

        We use:

            nextUpdate += interval_

        instead of:

            nextUpdate = now + interval_

        under normal conditions.

        This reduces accumulated timing drift.

        Example target times:

            1000 ms
            2000 ms
            3000 ms
            4000 ms

        instead of:

            wait 1000
            query takes 30
            wait 1000
            query takes 30
            ...

        which would gradually become:

            1030
            2060
            3090
            ...
        */

        nextUpdate += interval_;


        const auto now =
            Clock::now();


        /*
            If hardware sampling itself took longer than one complete
            interval, do NOT immediately execute several catch-up samples.

            Example:

                interval = 1000 ms
                one query unexpectedly takes 1500 ms

            We simply schedule the next sample from the current time.
        */

        if (nextUpdate <= now)
        {
            nextUpdate =
                now + interval_;
        }
    }
}


// ============================================================================
// UpdateOnce
// ============================================================================

void HardwareMonitor::UpdateOnce()
{
    /*
        Build a completely new snapshot first.

        dataMutex_ is deliberately NOT locked here.

        That means another thread calling:

            monitor.GetUsage()

        can still obtain the previous complete snapshot while the
        background worker is querying new hardware data.
    */

    HardwareUsage newUsage;


    // ------------------------------------------------------------------------
    // CPU
    // ------------------------------------------------------------------------

    newUsage.cpu =
        cpu_.GetUsage();


    // ------------------------------------------------------------------------
    // Memory
    // ------------------------------------------------------------------------

    newUsage.memory =
        memory_.GetUsage();


    // ------------------------------------------------------------------------
    // GPU
    // ------------------------------------------------------------------------

    newUsage.gpus =
        gpu_.GetUsages();


    /*
    ===========================================================================
    GPU count rule
    ===========================================================================

    The application supports at most two GPUs.

    Valid counts:

        0 GPU
        1 GPU
        2 GPUs

    Even if the underlying Windows GPU enumeration unexpectedly reports
    more adapters, only the first two are exposed by HardwareMonitor.

    This also guarantees that ConsolePrinter and the serial protocol see
    the same maximum GPU count.
    */

    if (newUsage.gpus.size() >
        MAX_GPU_COUNT)
    {
        newUsage.gpus.resize(
            MAX_GPU_COUNT
        );
    }


    // ------------------------------------------------------------------------
    // Local hardware sample sequence
    // ------------------------------------------------------------------------

    /*
        WorkerLoop() is the only thread that calls UpdateOnce(),
        therefore sequenceCounter_ does not need its own mutex.
    */

    newUsage.sequence =
        ++sequenceCounter_;


    // ------------------------------------------------------------------------
    // Publish new snapshot
    // ------------------------------------------------------------------------

    {
        std::lock_guard<std::mutex>
            lock(dataMutex_);

        /*
            The critical section is intentionally very short.

            Only the final complete snapshot is replaced here.
        */

        usage_ =
            std::move(newUsage);
    }
}


// ============================================================================
// GetUsage
// ============================================================================

HardwareUsage
HardwareMonitor::GetUsage() const
{
    /*
        GetUsage does NOT perform hardware sampling.

        It only copies the most recently completed snapshot.
    */

    std::lock_guard<std::mutex>
        lock(dataMutex_);

    return usage_;
}


// ============================================================================
// SetInterval
// ============================================================================

void HardwareMonitor::SetInterval(
    unsigned int intervalMs
)
{
    if (intervalMs == 0)
    {
        return;
    }

    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

        interval_ =
            std::chrono::milliseconds(
                intervalMs
            );

        /*
            Tell WorkerLoop that its current nextUpdate
            time is no longer valid.
        */

        intervalChanged_ = true;
    }


    /*
        Wake worker immediately so that it can recalculate:

            nextUpdate = now + new interval
    */

    condition_.notify_all();
}


// ============================================================================
// IsRunning
// ============================================================================

bool HardwareMonitor::IsRunning() const
{
    std::lock_guard<std::mutex>
        lock(controlMutex_);

    return running_;
}


// ============================================================================
// IsGpuAvailable
// ============================================================================

bool HardwareMonitor::IsGpuAvailable() const
{
    /*
        GpuMonitor availability is established during initialization
        and does not normally change while HardwareMonitor is running.
    */

    return gpu_.IsAvailable();
}
