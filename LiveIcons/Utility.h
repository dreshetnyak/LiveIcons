#pragma once
#include <string>
#include <Windows.h>
#include "StrLib.h"

namespace Utility
{
	using namespace std;

	wstring GetLastErrorStr();
	string ToAbsolutePath(const string& currentFilePath, const string& relativeFilePath);

	inline char FromHex(const char ch) { return static_cast<char>(isdigit(ch) ? ch - 0x30 : tolower(ch) - 'a' + 10); }
    inline bool IsHex(const char ch) { return ch > 47 && ch < 58 || ch > 64 && ch < 71 || ch > 96 && ch < 103; }
    
	string UrlDecode(const string& urlEncoded);

	class OnDestructor final
	{
		const function<void()>& Destructor;

	public:
		OnDestructor() = delete;
		OnDestructor(const OnDestructor& disposable) = delete;
		OnDestructor(OnDestructor&& disposable) = delete;
		OnDestructor& operator=(const OnDestructor& other) = delete;
		OnDestructor& operator=(OnDestructor&& other) = delete;

		explicit OnDestructor(const function<void()>& destructor) : Destructor(destructor) { }
		~OnDestructor() { Destructor(); }
	};
}