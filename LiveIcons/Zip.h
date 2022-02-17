#pragma once
#include <unzip.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "iowin32.h"
#include "StrLib.h"
#include "ZLibInternals.h"

namespace Zip
{
#define CASE_SENSITIVE 1
#define CASE_INSENSITIVE 2
#define STRINGS_EQUAL 0
#define UNKNOWN ((size_t)string::npos)
#define END_OF_LIST (nullptr)

	using namespace std;
}
