#include "SerialCommunicator.h"

#include "ConsoleLogger.h"

#include <array>
#include <utility>


// ============================================================================
// Constructor
// ============================================================================

SerialCommunicator::SerialCommunicator()
{}


// ============================================================================
// Destructor
// ============================================================================

SerialCommunicator::~SerialCommunicator()
{
    Close();
}


// ============================================================================
// Logger
// ============================================================================

void SerialCommunicator::SetLogger(
    ConsoleLogger* logger
)
{
    logger_.store(
        logger
    );
}


// ============================================================================
// Open
// ============================================================================

bool SerialCommunicator::Open(
    const std::wstring& portName,
    DWORD baudRate
)
{
    /*
        Close any previous session first.

        This also guarantees previous worker threads are gone
        before a new COM session starts.
    */

    Close();


    // ------------------------------------------------------------------------
    // Clear old queues
    // ------------------------------------------------------------------------

    {
        std::lock_guard<std::mutex>
            lock(txMutex_);

        txQueue_.clear();
    }


    {
        std::lock_guard<std::mutex>
            lock(rxMutex_);

        rxQueue_.clear();
    }


    // ------------------------------------------------------------------------
    // Open SerialPort
    // ------------------------------------------------------------------------

    if (!port_.Open(
        portName,
        baudRate))
    {
        ConsoleLogger* logger =
            logger_.load();

        if (logger != nullptr)
        {
            logger->LogSerialOpen(
                portName,
                baudRate,
                false
            );
        }

        return false;
    }


    // ------------------------------------------------------------------------
    // Serial opened successfully
    // ------------------------------------------------------------------------

    {
        ConsoleLogger* logger =
            logger_.load();

        if (logger != nullptr)
        {
            logger->LogSerialOpen(
                portName,
                baudRate,
                true
            );
        }
    }


    // Start packet sequence from zero for each connection.
    nextSequence_.store(
        0
    );


    running_.store(
        true
    );


    // ------------------------------------------------------------------------
    // Start worker threads
    // ------------------------------------------------------------------------

    try
    {
        txThread_ =
            std::thread(
                &SerialCommunicator::TxWorker,
                this
            );


        rxThread_ =
            std::thread(
                &SerialCommunicator::RxWorker,
                this
            );
    }
    catch (...)
    {
        /*
            Thread creation failure.

            One thread may already have been created,
            so perform a complete shutdown.
        */

        running_.store(
            false
        );


        txCondition_.notify_all();


        /*
            Cancel:

                WaitCommEvent
                ReadFile
                WriteFile

            if any worker already entered serial I/O.
        */

        port_.CancelPendingIO();


        if (txThread_.joinable())
        {
            txThread_.join();
        }


        if (rxThread_.joinable())
        {
            rxThread_.join();
        }


        port_.Close();


        ConsoleLogger* logger =
            logger_.load();

        if (logger != nullptr)
        {
            logger->Error(
                L"Failed to create serial worker threads."
            );
        }


        return false;
    }


    return true;
}


// ============================================================================
// Close
// ============================================================================

void SerialCommunicator::Close()
{
    /*
        Nothing to do if:

            not running
            AND
            port is already closed.
    */

    if (!running_.load() &&
        !port_.IsOpen())
    {
        return;
    }


    // ------------------------------------------------------------------------
    // Tell workers to stop
    // ------------------------------------------------------------------------

    running_.store(
        false
    );


    /*
        TX thread may be asleep on condition_variable.
    */

    txCondition_.notify_all();


    /*
        RX thread may currently be blocked in:

            WaitCommEvent
            ReadFile

        TX may be in:

            WriteFile

        Cancel pending OVERLAPPED I/O so workers can exit quickly.
    */

    port_.CancelPendingIO();


    // ------------------------------------------------------------------------
    // Join TX worker
    // ------------------------------------------------------------------------

    if (txThread_.joinable())
    {
        txThread_.join();
    }


    // ------------------------------------------------------------------------
    // Join RX worker
    // ------------------------------------------------------------------------

    if (rxThread_.joinable())
    {
        rxThread_.join();
    }


    // ------------------------------------------------------------------------
    // Close Windows COM handle
    // ------------------------------------------------------------------------

    port_.Close();


    // ------------------------------------------------------------------------
    // Clear queues
    // ------------------------------------------------------------------------

    {
        std::lock_guard<std::mutex>
            lock(txMutex_);

        txQueue_.clear();
    }


    {
        std::lock_guard<std::mutex>
            lock(rxMutex_);

        rxQueue_.clear();
    }


    // ------------------------------------------------------------------------
    // Log
    // ------------------------------------------------------------------------

    ConsoleLogger* logger =
        logger_.load();

    if (logger != nullptr)
    {
        logger->LogSerialClose();
    }
}


// ============================================================================
// IsRunning
// ============================================================================

bool SerialCommunicator::IsRunning() const
{
    return running_.load();
}


// ============================================================================
// SendPacket
// ============================================================================

bool SerialCommunicator::SendPacket(
    PacketType type,
    const std::vector<std::uint8_t>& payload,
    std::uint8_t flags
)
{
    // ------------------------------------------------------------------------
    // Communication must be active
    // ------------------------------------------------------------------------

    if (!running_.load())
    {
        return false;
    }


    // ------------------------------------------------------------------------
    // Validate payload
    // ------------------------------------------------------------------------

    if (payload.size() >
        PacketProtocol::MAX_PAYLOAD_SIZE)
    {
        ConsoleLogger* logger =
            logger_.load();

        if (logger != nullptr)
        {
            logger->Error(
                L"Serial packet payload exceeds MAX_PAYLOAD_SIZE."
            );
        }

        return false;
    }


    // ------------------------------------------------------------------------
    // Create logical packet
    // ------------------------------------------------------------------------

    Packet packet;


    packet.type =
        type;


    packet.flags =
        flags;


    /*
        fetch_add() returns the old value.

        Example:

            initial nextSequence = 0

            packet #1 -> sequence 0
            packet #2 -> sequence 1
            packet #3 -> sequence 2
    */

    packet.sequence =
        nextSequence_.fetch_add(
            1
        );


    packet.payload =
        payload;


    // ------------------------------------------------------------------------
    // Encode packet
    // ------------------------------------------------------------------------

    TxItem item;


    item.packet =
        packet;


    if (!PacketEncoder::Encode(
        packet,
        item.raw))
    {
        ConsoleLogger* logger =
            logger_.load();

        if (logger != nullptr)
        {
            logger->Error(
                L"Serial packet encoding failed."
            );
        }

        return false;
    }


    // ------------------------------------------------------------------------
    // Add to TX queue
    // ------------------------------------------------------------------------

    {
        std::lock_guard<std::mutex>
            lock(txMutex_);


        /*
            The communicator could have been closed after the first
            running_ check but before acquiring this mutex.

            Check once more before queuing.
        */

        if (!running_.load())
        {
            return false;
        }


        if (txQueue_.size() >=
            MAX_TX_QUEUE_SIZE)
        {
            ConsoleLogger* logger =
                logger_.load();

            if (logger != nullptr)
            {
                logger->Warning(
                    L"Serial TX queue is full. Packet dropped."
                );
            }

            return false;
        }


        txQueue_.push_back(
            std::move(item)
        );
    }


    // Wake TX thread.
    txCondition_.notify_one();


    return true;
}


// ============================================================================
// TryReceive
// ============================================================================

bool SerialCommunicator::TryReceive(
    Packet& packet
)
{
    std::lock_guard<std::mutex>
        lock(rxMutex_);


    if (rxQueue_.empty())
    {
        return false;
    }


    packet =
        std::move(
            rxQueue_.front()
        );


    rxQueue_.pop_front();


    return true;
}


// ============================================================================
// SetReceiveCallback
// ============================================================================

void SerialCommunicator::SetReceiveCallback(
    ReceiveCallback callback
)
{
    std::lock_guard<std::mutex>
        lock(callbackMutex_);


    receiveCallback_ =
        std::move(callback);
}


// ============================================================================
// TX Worker
// ============================================================================

void SerialCommunicator::TxWorker()
{
    while (true)
    {
        TxItem item;


        // --------------------------------------------------------------------
        // Wait for data
        // --------------------------------------------------------------------

        {
            std::unique_lock<std::mutex>
                lock(txMutex_);


            txCondition_.wait(
                lock,
                [this]()
                {
                    return
                        !running_.load()
                        ||
                        !txQueue_.empty();
                }
            );


            /*
                Stop requested.

                We intentionally do not send packets remaining in the
                queue during shutdown.
            */

            if (!running_.load())
            {
                break;
            }


            if (txQueue_.empty())
            {
                continue;
            }


            item =
                std::move(
                    txQueue_.front()
                );


            txQueue_.pop_front();
        }


        // --------------------------------------------------------------------
        // Skip invalid empty encoded data
        // --------------------------------------------------------------------

        if (item.raw.empty())
        {
            continue;
        }


        // --------------------------------------------------------------------
        // Perform actual COM transmission
        // --------------------------------------------------------------------

        const bool success =
            port_.Write(
                item.raw.data(),
                static_cast<DWORD>(
                    item.raw.size()
                    )
            );


        if (!success)
        {
            /*
                If Close() intentionally stopped communication,
                Write() can fail because CancelIoEx() aborted it.

                That is not considered an unexpected error.
            */

            if (running_.load())
            {
                HandleCommunicationFailure(
                    L"Serial COM write failed."
                );
            }

            break;
        }


        // --------------------------------------------------------------------
        // TX debug log
        // --------------------------------------------------------------------

        ConsoleLogger* logger =
            logger_.load();


        if (logger != nullptr)
        {
            /*
                Log only after SerialPort::Write() succeeded.

                Therefore [SERIAL TX] means the COM write operation
                completed successfully.
            */

            logger->LogTxPacket(
                item.packet,
                item.raw
            );
        }
    }
}


// ============================================================================
// RX Worker
// ============================================================================

void SerialCommunicator::RxWorker()
{
    /*
        Temporary raw UART receive buffer.

        PacketParser handles:

            packet splitting
            packet merging
            partial packets

        so this size does NOT need to match packet size.
    */

    std::array<
        std::uint8_t,
        256
    > readBuffer{};


    PacketParser parser;


    while (running_.load())
    {
        DWORD bytesRead = 0;


        // --------------------------------------------------------------------
        // Read some UART bytes
        // --------------------------------------------------------------------

        const bool result =
            port_.ReadSome(
                readBuffer.data(),
                static_cast<DWORD>(
                    readBuffer.size()
                    ),
                bytesRead,
                200
            );


        if (!result)
        {
            /*
                CancelIoEx() during Close() also causes pending reads
                to terminate.

                Do not report that as an error if shutdown was intentional.
            */

            if (running_.load())
            {
                HandleCommunicationFailure(
                    L"Serial COM read failed."
                );
            }

            break;
        }


        /*
            ReadSome timeout:

                result == true
                bytesRead == 0

            This is normal.

            It simply means no UART bytes arrived during this period.
        */

        if (bytesRead == 0)
        {
            continue;
        }


        // --------------------------------------------------------------------
        // Raw RX debug output
        // --------------------------------------------------------------------

        {
            ConsoleLogger* logger =
                logger_.load();


            if (logger != nullptr)
            {
                logger->LogRxRaw(
                    readBuffer.data(),
                    bytesRead
                );
            }
        }


        // --------------------------------------------------------------------
        // Feed stream parser
        // --------------------------------------------------------------------

        parser.PushBytes(
            readBuffer.data(),
            bytesRead
        );


        // --------------------------------------------------------------------
        // Extract every complete packet currently available
        // --------------------------------------------------------------------

        while (true)
        {
            Packet packet;


            if (!parser.TryGetPacket(
                packet))
            {
                break;
            }


            /*
                If PacketParser returned a packet, it has already verified:

                    AA 55
                    protocol version
                    payload length
                    CRC16
            */


            // ----------------------------------------------------------------
            // Decoded packet debug output
            // ----------------------------------------------------------------

            {
                ConsoleLogger* logger =
                    logger_.load();


                if (logger != nullptr)
                {
                    logger->LogRxPacket(
                        packet
                    );
                }
            }


            // ----------------------------------------------------------------
            // Deliver packet
            // ----------------------------------------------------------------

            HandleReceivedPacket(
                packet
            );
        }
    }
}


// ============================================================================
// HandleReceivedPacket
// ============================================================================

void SerialCommunicator::HandleReceivedPacket(
    const Packet& packet
)
{
    // ========================================================================
    // RX queue
    // ========================================================================

    {
        std::lock_guard<std::mutex>
            lock(rxMutex_);


        /*
            If the consumer is too slow, keep newer information.

            Example:

                queue size = 128
                new packet arrives

            Drop:

                oldest packet

            Then insert:

                newest packet
        */

        if (rxQueue_.size() >=
            MAX_RX_QUEUE_SIZE)
        {
            rxQueue_.pop_front();


            ConsoleLogger* logger =
                logger_.load();


            if (logger != nullptr)
            {
                logger->Warning(
                    L"Serial RX queue full. Oldest packet dropped."
                );
            }
        }


        rxQueue_.push_back(
            packet
        );
    }


    // ========================================================================
    // Optional callback
    // ========================================================================

    ReceiveCallback callback;


    {
        /*
            Copy callback while holding mutex.

            Execute callback only after mutex has been released.

            This prevents the callback itself from blocking
            SetReceiveCallback().
        */

        std::lock_guard<std::mutex>
            lock(callbackMutex_);


        callback =
            receiveCallback_;
    }


    if (callback)
    {
        /*
            IMPORTANT:

            callback runs on RX worker thread.

            Keep it short.
        */

        callback(
            packet
        );
    }
}


// ============================================================================
// Communication failure
// ============================================================================

void SerialCommunicator::HandleCommunicationFailure(
    const std::wstring& message
)
{
    /*
        Multiple workers could discover the same connection failure.

        exchange(false) returns the previous state.

        Only the first worker that changes:

            true -> false

        reports the error.
    */

    const bool wasRunning =
        running_.exchange(
            false
        );


    if (wasRunning)
    {
        ConsoleLogger* logger =
            logger_.load();


        if (logger != nullptr)
        {
            logger->Error(
                message
            );
        }
    }


    /*
        Wake TX worker if it is waiting.
    */

    txCondition_.notify_all();


    /*
        Cancel pending RX/TX overlapped operations.

        This helps the other worker exit immediately.
    */

    port_.CancelPendingIO();
}
