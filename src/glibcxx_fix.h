// Workaround for ESP32 toolchain 8.4.0 bug where
// stdlib headers reference debug symbols without proper includes.
#ifndef __glibcxx_requires_string
#define __glibcxx_requires_string(_String)
#endif
#ifndef __glibcxx_requires_string_len
#define __glibcxx_requires_string_len(_String, _Len)
#endif
#ifndef _GLIBCXX_DEBUG_ASSERT
#define _GLIBCXX_DEBUG_ASSERT(_Condition)
#endif

#ifdef __cplusplus
namespace __gnu_debug {
    enum {
        __msg_inc_istreambuf,
        __msg_dec_istreambuf,
        __msg_deref_istreambuf,
        __msg_inc_ostreambuf
    };
}

// Provide _M_message as no-op template
namespace std {
    template<typename T>
    struct __istreambuf_iterator_base {
        void _M_message(int) {}
    };
}
#endif
