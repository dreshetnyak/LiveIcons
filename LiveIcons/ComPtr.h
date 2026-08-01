#pragma once

#include <utility>

#include "Log.h"

template<typename Interface>
class ComPtr final
{
	Interface* Pointer{};

public:
	ComPtr() noexcept = default;
	explicit ComPtr(Interface* pointer) noexcept : Pointer{ pointer } { }

	ComPtr(const ComPtr&) = delete;
	ComPtr& operator=(const ComPtr&) = delete;

	ComPtr(ComPtr&& other) noexcept : Pointer{ other.Detach() } { }

	ComPtr& operator=(ComPtr&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			Pointer = other.Detach();
		}
		return *this;
	}

	~ComPtr() noexcept
	{
		Reset();
	}

	[[nodiscard]] Interface* Get() const noexcept { return Pointer; }
	[[nodiscard]] Interface* operator->() const noexcept { return Pointer; }
	explicit operator bool() const noexcept { return Pointer != nullptr; }

	[[nodiscard]] Interface** Put() noexcept
	{
		Reset();
		return &Pointer;
	}

	[[nodiscard]] Interface* Detach() noexcept
	{
		return std::exchange(Pointer, nullptr);
	}

	void Reset(Interface* pointer = nullptr) noexcept
	{
		if (Pointer == pointer)
			return;

		Interface* const previous = std::exchange(Pointer, pointer);
		if (previous == nullptr)
			return;

		try
		{
			previous->Release();
		}
		catch (...)
		{
			// COM forbids exceptions across the ABI. Cleanup remains best-effort for
			// a non-conforming third-party implementation.
			Log::Exception(Log::EventId::ComRelease, E_UNEXPECTED, 0,
				"nonconforming-release");
		}
	}
};
