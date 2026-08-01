#pragma once
#include "ioapi.h"

namespace ZipStream
{
    using namespace std;

    struct StreamInfo
    {
        IStream* FileStream{};
        HRESULT LastResult{};
    };

    voidpf ZCALLBACK CastFileNamePtrToIStream(voidpf opaque, const void* filename, int mode) noexcept;

    uLong ZCALLBACK Read(voidpf opaque, voidpf stream, void* buf, uLong size) noexcept;

    uLong ZCALLBACK Write(voidpf opaque, voidpf stream, const void* buf, uLong size) noexcept;

    ZPOS64_T ZCALLBACK Tell(voidpf opaque, voidpf stream) noexcept;

    long ZCALLBACK Seek(voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) noexcept;

    int ZCALLBACK Close(voidpf opaque, voidpf stream) noexcept;

    int ZCALLBACK Error(voidpf opaque, voidpf stream) noexcept;

    void SetIStreamHandlers(zlib_filefunc64_def* handlers) noexcept;
}
