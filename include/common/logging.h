#pragma once
#include "util.h"

#define STLOG(level)                                                           \
    LOG(level) << ::systrace::util::config::GlobalConfig::rank_str

namespace systrace
{
void setLoggingPath();
}