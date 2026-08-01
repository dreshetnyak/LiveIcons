#pragma once

#include <cstdint>
#include <exception>
#include <new>
#include <utility>

#include "Log.h"

namespace ExceptionBoundary
{
	template<typename Action>
	[[nodiscard]] HRESULT ToHResult(
		const Log::EventId eventId,
		const std::uint64_t correlationId,
		Action&& action) noexcept
	{
		try
		{
			return std::forward<Action>(action)();
		}
		catch (const std::bad_alloc&)
		{
			Log::Exception(eventId, E_OUTOFMEMORY, correlationId, "bad_alloc");
			return E_OUTOFMEMORY;
		}
		catch (const std::exception&)
		{
			Log::Exception(eventId, E_UNEXPECTED, correlationId, "std_exception");
			return E_UNEXPECTED;
		}
		catch (...)
		{
			Log::Exception(eventId, E_UNEXPECTED, correlationId, "unknown_exception");
			return E_UNEXPECTED;
		}
	}
}
