#pragma once

class ReferenceCounter
{
	std::mutex CounterLock{};
	unsigned long Counter = 0;

public:
	unsigned long Increment()
	{
		CounterLock.lock();
		++Counter;
		const auto counter = Counter;
		CounterLock.unlock();
		return counter;
	}

	unsigned long Decrement()
	{
		CounterLock.lock();
		if (Counter != 0)
			--Counter;
		const auto counter = Counter;
		CounterLock.unlock();
		return counter;
	}

	bool NoReference()
	{
		CounterLock.lock();
		const auto noReference = Counter == 0;
		CounterLock.unlock();
		return noReference;		
	}
};

