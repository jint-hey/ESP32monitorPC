#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

template <std::size_t Capacity>
class ByteRingBuffer
{
public:
    static_assert(Capacity > 0);

    std::size_t Size() const
    {
        return size_;
    }

    std::size_t OverflowCount() const
    {
        return overflowCount_;
    }

    void Clear()
    {
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

    void Push(
        const uint8_t* data,
        std::size_t length
    )
    {
        if (data == nullptr)
        {
            return;
        }

        for (std::size_t index = 0;
            index < length;
            ++index)
        {
            if (size_ == Capacity)
            {
                head_ = (head_ + 1) % Capacity;
                --size_;
                ++overflowCount_;
            }

            data_[tail_] = data[index];
            tail_ = (tail_ + 1) % Capacity;
            ++size_;
        }
    }

    uint8_t Peek(std::size_t index) const
    {
        return data_[(head_ + index) % Capacity];
    }

    void Copy(
        std::size_t offset,
        uint8_t* destination,
        std::size_t length
    ) const
    {
        if (destination == nullptr)
        {
            return;
        }

        for (std::size_t index = 0;
            index < length;
            ++index)
        {
            destination[index] = Peek(offset + index);
        }
    }

    void Discard(std::size_t length)
    {
        if (length > size_)
        {
            length = size_;
        }

        head_ = (head_ + length) % Capacity;
        size_ -= length;

        if (size_ == 0)
        {
            tail_ = head_;
        }
    }

private:
    std::array<uint8_t, Capacity> data_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;
    std::size_t overflowCount_ = 0;
};
