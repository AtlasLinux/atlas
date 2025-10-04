#pragma once
#include <stdarg.h>
#include <stddef.h>


typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;
typedef struct Logger Logger;
typedef struct LogSink LogSink;

struct LogSink {
    void (*write)(LogSink* self, LogLevel lvl, const char* msg);
    void (*destroy)(LogSink* self);
};

LogSink* console_sink_create(void);
LogSink* file_sink_create(const char* filename);

Logger* logger_create(LogLevel level);
void logger_destroy(Logger* logger);
void logger_add_sink(Logger* logger, LogSink* sink);
void logger_set_level(Logger* logger, LogLevel level);
void logger_log(Logger* logger, LogLevel level, const char* fmt, ...);

const char* log_level_to_string(LogLevel level);

Logger* logger;

#define log_debug(fmt, ...) logger_log(logger, LOG_DEBUG, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...)  logger_log(logger, LOG_INFO, fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...)  logger_log(logger, LOG_WARN, fmt, ##__VA_ARGS__)
#define log_error(fmt, ...) logger_log(logger, LOG_ERROR, fmt, ##__VA_ARGS__)
#define log_perror(fmt)     logger_log(logger, LOG_ERROR, fmt, strerror(errno))