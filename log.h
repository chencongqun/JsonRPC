#ifndef _LOG__H
#define _LOG__H

enum
{
    LOGTYPE_STDERR,
    LOGTYPE_SYSLOG,
};

enum
{
    LOG_Error,
    LOG_Warning,
    LOG_Info,
    LOG_Debug,
};

void simple_log( int i_loglevel, const char *psz_format, ... );

#define SLOG_ERROR( ... ) simple_log( LOG_Error, __VA_ARGS__ );
#define SLOG_WARNING( ... ) simple_log( LOG_Warning, __VA_ARGS__ );
#define SLOG_INFO( ... ) simple_log( LOG_Info, __VA_ARGS__ );
#define SLOG_DEBUG( ... ) simple_log( LOG_Debug, __VA_ARGS__ );




#endif

