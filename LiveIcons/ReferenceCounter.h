#pragma once

#include <atomic>

class ReferenceCounter
{
	std::atomic<unsigned long> Counter;

public:
	explicit constexpr ReferenceCounter(const unsigned long initialValue = 0) noexcept :
		Counter{ initialValue }
	{
	}

	ReferenceCounter(const ReferenceCounter&) = delete;
	ReferenceCounter& operator=(const ReferenceCounter&) = delete;

	unsigned long Increment() noexcept
	{
		return Counter.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	unsigned long Decrement() noexcept
	{
		auto current = Counter.load(std::memory_order_relaxed);
		while (current != 0)
		{
			if (Counter.compare_exchange_weak(
				current, current - 1,
				std::memory_order_acq_rel, std::memory_order_relaxed))
				return current - 1;
		}

		// Keep an unbalanced release from wrapping the module count to ULONG_MAX.
		return 0;
	}

	[[nodiscard]] bool NoReference() const noexcept
	{
		return Counter.load(std::memory_order_acquire) == 0;
	}
};

