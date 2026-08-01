#pragma once
#include <cstdint>
#include <Windows.h>

namespace Log
{
	enum class EventId : std::uint16_t
	{
		Logger,
		DllGetClassObject,
		DllRegisterServer,
		DllUnregisterServer,
		ClassFactoryCreate,
		ClassFactoryQueryInterface,
		ClassFactoryLockServer,
		LiveIconsCreate,
		LiveIconsQueryInterface,
		LiveIconsInitialize,
		Thumbnail,
		ComRelease,
		Registry,
		DecodeBase64
	};

	// Correlation identifiers are process-local and intended to connect the
	// records emitted while servicing one thumbnail request.
	[[nodiscard]] std::uint64_t NextCorrelationId() noexcept;

	// detail must be a short, static category such as "bad-allocation". Never
	// pass a document path, archive member name, file content, or other user data.
	void Diagnostic(EventId eventId, HRESULT result, std::uint64_t correlationId = 0,
		const char* detail = nullptr) noexcept;
	void Error(EventId eventId, HRESULT result, std::uint64_t correlationId = 0,
		const char* detail = nullptr) noexcept;
	void Exception(EventId eventId, HRESULT result, std::uint64_t correlationId = 0,
		const char* detail = nullptr) noexcept;
}

