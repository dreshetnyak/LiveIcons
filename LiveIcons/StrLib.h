#pragma once
#include <locale>
#include <string>
#include <vector>
#include <Windows.h>

namespace StrLib
{
    using namespace std;

    string ToString(const wstring& wideString);
    wstring ToWstring(const string& multiByteString);
    wstring ToWstring(LPCCH multiByteString, int size);
    void UnEscapeXml(string& str);

    inline int ToLower(const int ch) { return (ch > 64 && ch < 91) ? (ch + 32) : ch; }

    inline bool EqualsCi(const int chA, const int chB)
    {
        return chA == chB || ToLower(chA) == ToLower(chB);
    }

    template<class T>
    bool EqualsCi(const basic_string<T>& strA, const basic_string<T>& strB)
    {
        const auto sizeA = strA.size();
        if (strB.size() != sizeA)
            return false;

        const auto size = static_cast<int>(sizeA);
        for (auto index = 0; index < size; ++index)
        {
            if (!EqualsCi(strA[index], strB[index]))
                return false;
        }

        return true;
    }

    template<typename T>
    size_t FindCi(const basic_string<T>& str, const basic_string<T>& findStr, const size_t offset)
    {
        const auto strSize = str.size();
        if (strSize == 0)
            return basic_string<T>::npos;
        const auto findStrSize = findStr.size();
        if (findStrSize == 0 || strSize < findStrSize)
            return basic_string<T>::npos;
        const auto strLimit = strSize - findStrSize;

        for (auto strIndex = offset; strIndex <= strLimit; ++strIndex)
        {
            auto match = true;
            for (auto findStrIndex = 0; findStrIndex < findStrSize; ++findStrIndex)
            {
                if (EqualsCi(str[strIndex + findStrIndex], findStr[findStrIndex]))
                    continue;
                match = false;
                break;
            }

            if (match)
                return strIndex;
        }

        return basic_string<T>::npos;
    }

    template<typename T>
    size_t FindReverse(const basic_string<T>& str, const T ch, size_t offset = basic_string<T>::npos)
    {
        const auto strSize = str.size();
        if (strSize == 0)
            return basic_string<T>::npos;
        if (offset == basic_string<T>::npos)
            offset = strSize - 1;

        for (int index = static_cast<int>(offset > strSize ? strSize - 1 : offset); index >= 0; --index)
        {
            if (str[index] == ch)
                return index;
        }

        return basic_string<T>::npos;
    }

    template<class T>
    bool EqualsCiOneOf(const basic_string<T>& str, const vector<basic_string<T>> strings)
    {
        const auto strSize = str.size();
        for (const auto& oneOfStrings : strings)
        {
            if (oneOfStrings.size() != strSize)
                continue;

            auto stringsEqual = true;
            for (auto index = 0; index < strSize; ++index)
            {
                if (EqualsCi(str[index], oneOfStrings[index]))
                    continue;
                stringsEqual = false;
                break;
            }

            if (stringsEqual)
                return true;
        }

        return false;
    }

    template<class T>
    bool ContainsCiOneOf(const basic_string<T>& str, const vector<basic_string<T>> strings)
    {
        const auto strSize = str.size();
        for (const auto& oneOfStrings : strings)
        {
            if (FindCi(str, oneOfStrings, 0) != basic_string<T>::npos)
                return true;
        }

        return false;
    }

    template<typename T>
    bool StartsWith(const basic_string<T> str, const basic_string<T>& strStart, size_t offset)
    {
        const auto strSize = str.size();
        if (strSize == 0)
            return false;
        const auto startStrSize = strStart.size();
        if (startStrSize == 0 || strSize - offset < startStrSize)
            return false;

        for (auto index = 0; index < startStrSize; ++index)
        {
            if (!EqualsCi(str[offset + index], strStart[index]))
                return false;
        }

        return true;
    }

    template<typename T>
    bool EndsWith(const basic_string<T> str, const basic_string<T>& strEnd)
    {
        const auto strSize = str.size();
        if (strSize == 0)
            return false;
        const auto endStrSize = strEnd.size();
        if (endStrSize == 0 || strSize < endStrSize)
            return false;

        for (size_t index = 0, strOffset = strSize - endStrSize; index < endStrSize; ++index)
        {
            if (!EqualsCi(str[strOffset + index], strEnd[index]))
                return false;
        }

        return true;
    }

    template<typename T>
    bool EndsWith(const basic_string<T> str, const vector<basic_string<T>> strEnds)
    {
        for (const auto& strEnd : strEnds)
        {
            if (EndsWith(str, strEnd))
                return true;
        }

        return false;
    }

    template<typename T>
    void Remove(basic_string<T>& str, const size_t offset, const size_t count)
    {
        auto readOffset = offset + count;
        const auto strLength = str.length();
        if (readOffset < strLength)
        {
            memcpy(static_cast<void*>(&str[offset]), static_cast<void*>(&str[readOffset]), (strLength - readOffset) * sizeof T);
            str.resize(strLength - count);
        }
        else
            str.resize(offset);
    }

    template<typename T>
    void ReplaceAll(basic_string<T>& str, const basic_string<T>& from, const basic_string<T>& to)
    {
        if (from.empty())
            return;

        size_t offset{ 0 };
        while ((offset = FindCi(str, from, offset)) != basic_string<T>::npos)
        {
            str.replace(offset, from.length(), to);
            offset += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
        }
    }

    template<typename T>
    void ReplaceAll(basic_string<T>& str, const T from, const T to)
    {
        const auto strSize = str.size();
        for (size_t index{ 0 }; index < strSize; ++index)
        {
            if (str[index] == from)
                str[index] = to;
        }
    }

    template<typename T>
    void TrimStartCi(basic_string<T>& str, const basic_string<T>& trim)
    {
        const auto strLength = str.length();
        const auto trimLength = trim.length();
        for (size_t offset{ 0 }; offset < strLength; offset += trimLength)
        {
            if (StartsWith(str, trim, offset))
                continue;
            if (offset == 0)
                return;
            Remove(str, 0, offset);
            return;
        }

        str.resize(static_cast<size_t>(0));
    }

    template<typename T>
    void TrimStartCi(basic_string<T>& str, const T ch)
    {
        for (size_t index{ 0 }; index < str.length(); ++index)
        {
            if (EqualsCi(str[index], ch))
                continue;
            if (index == 0)
                return;
            Remove(str, 0, index);
            return;
        }

        str.resize(static_cast<size_t>(0));
    }

    template<typename T>
    vector<basic_string<T>> Split(const basic_string<T>& str, T splitCh, const bool preserveEmpty = false)
    {
        vector<basic_string<T>> splitStr;
        const auto strSize = str.size();
        size_t prevChOffset = 0;
        for (size_t chOffset = 0; chOffset < strSize && (chOffset = str.find(splitCh, chOffset)) != basic_string<T>::npos; prevChOffset = ++chOffset)
        {
            if (const auto subStrSize = chOffset - prevChOffset; subStrSize > 0)
                splitStr.push_back(str.substr(prevChOffset, subStrSize));
            else if (preserveEmpty)
                splitStr.push_back(basic_string<T>{});
        }

        splitStr.push_back(str.substr(splitStr.size() != 0 ? prevChOffset : 0));
        return splitStr;
    }

    template<typename T>
    basic_string<T> Join(const vector<basic_string<T>>& src, const basic_string<T> divider)
    {
        const auto srcSize = src.size();
        if (srcSize == 0)
            return basic_string<T>{};
        basic_string<T> joinedStr{ src[0] };
        for (auto srcItemIndex = 1; srcItemIndex < srcSize; ++srcItemIndex)
        {
            joinedStr.append(divider);
            joinedStr.append(src[srcItemIndex]);
        }

        return joinedStr;
    }

    template<typename T>
    basic_string<T> PreserveLeftOfLast(const basic_string<T>& src, const T ch) //TODO BUG!!! ch is not used
    {
        const auto strEndOffset = src.find_last_of('/');
        return strEndOffset != basic_string<T>::npos && strEndOffset != 0
            ? src.substr(0, strEndOffset)
            : basic_string<T>{};
    }

    template<typename T>
    void Filter(basic_string<T> src, const basic_string<T>& removeChars)
    {
        size_t writeIndex{ 0 };
        const auto srcSize = src.size();
        for (size_t readIndex = 0; readIndex < srcSize; ++readIndex)
        {
            const auto ch = src[readIndex];
            if (removeChars.find_first_of(ch) == basic_string<T>::npos)
                src[writeIndex++] = ch;
        }

        if (writeIndex != srcSize)
            src.resize(writeIndex);
    }
}