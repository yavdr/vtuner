#ifndef _VTUNERUTILS_H_
#define _VTUNERUTILS_H_

extern int dbg_level;
extern int use_syslog;

#define ERROR(msg, ...) write_message(0x01, "[%d %s:%u] error: " msg, getpid(), __FILE__, __LINE__, ## __VA_ARGS__)
#define  WARN(msg, ...) write_message(0x02, "[%d %s:%u] warn: " msg,  getpid(), __FILE__, __LINE__, ## __VA_ARGS__)
#define  INFO(msg, ...) write_message(0x04, "[%d %s:%u] info: " msg,  getpid(), __FILE__, __LINE__, ## __VA_ARGS__)

void write_message(int, const char*, ...);
#endif
