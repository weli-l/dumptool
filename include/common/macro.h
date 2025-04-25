#pragma once
#define EXPOSE_API __attribute__((visibility("default")))

#define STRINGIFY(x) #x
#define CANN_SYMBOL_STRING(x) STRINGIFY(x)

#define SETUP_DLSYM(fn_name)                                                   \
    if (__builtin_expect(!(orig_##fn_name), 0))                                \
    {                                                                          \
        orig_##fn_name =                                                       \
            (fn_name##Fn)dlsym(RTLD_NEXT, CANN_SYMBOL_STRING(fn_name));        \
        if (!orig_##fn_name)                                                   \
        {                                                                      \
            LOG(ERROR) << "Get origin " << CANN_SYMBOL_STRING(fn_name)         \
                       << " failed!";                                          \
            std::exit(1);                                                      \
        }                                                                      \
    }

#define SETUP_SYMBOL_FOR_LOAD_LIBRARY(handle, symbol, func_ptr, func_type,     \
                                      msg)                                     \
    do                                                                         \
    {                                                                          \
        func_ptr = (func_type)dlsym(handle, symbol);                           \
        const char *dlsym_error = dlerror();                                   \
        if (dlsym_error)                                                       \
        {                                                                      \
            STLOG(WARNING) << "Load fn `" << symbol << "` error in " << msg    \
                           << dlsym_error;                                     \
            can_use_ = false;                                                  \
            return;                                                            \
        }                                                                      \
    } while (0)
