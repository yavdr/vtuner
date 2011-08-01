#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <stdarg.h>

#define MAX_MSGSIZE 1024
#include "vtuner-utils.h"

__thread char msg[MAX_MSGSIZE];

void write_message(int level, const char* fmt, ... ) {
  if( level & dbg_level  ) {

    char tn[MAX_MSGSIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tn, sizeof(tn), fmt, ap);
    va_end(ap);
    strncat(msg, tn, sizeof(msg));

    if(use_syslog) {
      int priority;
      switch(level) {
        case 0: priority=LOG_ERR; break;
        case 1: priority=LOG_WARNING; break;
        case 2: priority=LOG_INFO; break;
        default: priority=LOG_DEBUG; break;
      }
      syslog(priority, "%s", msg);
    } else {
      fprintf(stderr, "%s", msg);
    }
  } 

  strncpy(msg, "", sizeof(msg));
}

void init_message(const char* fmt, ... ) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
}

void append_message(const char* fmt, ... ) {
  char tn[MAX_MSGSIZE]; 
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(tn, sizeof(tn), fmt, ap);
  va_end(ap);

  strncat(msg, tn, sizeof(msg));
}  
