#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

typedef struct {
    int type;
    const char* frmt;
    struct tm *time;
} log_event;

enum { DEBUG, INFO, WARN, FATAL };

#define log_debug(...) call_event(DEBUG, __VA_ARGS__)
#define log_info(...) call_event(INFO, __VA_ARGS__)
#define log_warn(...) call_event(WARN, __VA_ARGS__)
#define log_fatal(...) call_event(FATAL, __VA_ARGS__)

void call_event(int type, const char *frmt, ...);

#endif