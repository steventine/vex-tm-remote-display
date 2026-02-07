#pragma once

#include <stdio.h>
#include <stdarg.h>

// Simple logging macros that don't depend on OBS
#define LOG_DEBUG 0
#define LOG_INFO 1
#define LOG_WARNING 2
#define LOG_ERROR 3

static inline void simple_log(int level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const char* prefix;
    switch(level) {
        case LOG_DEBUG: prefix = "[DEBUG]"; break;
        case LOG_INFO: prefix = "[INFO]"; break;
        case LOG_WARNING: prefix = "[WARN]"; break;
        case LOG_ERROR: prefix = "[ERROR]"; break;
        default: prefix = "[LOG]"; break;
    }
    fprintf(stderr, "%s ", prefix);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

#define debug(format, ...) simple_log(LOG_DEBUG, format, ##__VA_ARGS__)
#define info(format, ...) simple_log(LOG_INFO, format, ##__VA_ARGS__)
#define warn(format, ...) simple_log(LOG_WARNING, format, ##__VA_ARGS__)
#define error(format, ...) simple_log(LOG_ERROR, format, ##__VA_ARGS__)

