#pragma once

extern bool logWindowOpen;

#if ENABLE_MESSAGE_LOG

#ifdef MESSAGE_LOG_TO_STDOUT
#define LOG_MESSAGE(message, ...) printf(message, __VA_ARGS__)
#define LOG_WARNING(message, ...) printf(message, __VA_ARGS__)
#define LOG_ERROR(message, ...) printf(message, __VA_ARGS__)
#else

enum log_message_type
{
	log_message_type_normal,
	log_message_type_warning,
	log_message_type_error,

	log_message_type_count,
};

#define LOG_MESSAGE(message, ...) logMessageInternal(log_message_type_normal, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#define LOG_WARNING(message, ...) logMessageInternal(log_message_type_warning, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#define LOG_ERROR(message, ...) logMessageInternal(log_message_type_error, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)

void logMessageInternal(log_message_type type, const char* file, const char* function, uint32 line, const char* format, ...);

void initializeMessageLog();
void updateMessageLog(float dt);

#endif

#else
#define LOG_MESSAGE(...)
#define LOG_WARNING(...)
#define LOG_ERROR(...)

#define initializeMessageLog(...)
#define updateMessageLog(...)

#endif
