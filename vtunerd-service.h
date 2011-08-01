#ifndef _VTUNERDSERVICE_H_
#define _VTUNEDRSERVICE_H_

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "vtuner-network.h"

#if HAVE_DVB_API_VERSION < 3
  #include "vtuner-dmm-2.h"
#else
  #ifdef HAVE_DREAMBOX_HARDWARE
    #include "vtuner-dmm-3.h"
  #else
    #include "vtuner-dvb-3.h"
  #endif
#endif

#define DEBUGSRV(msg, ...)  write_message(0x0040, "[%d %s:%u] debug: " msg, getpid(), __FILE__, __LINE__, ## __VA_ARGS__)
#define DEBUGSRVI(msg, ...) init_message((0x0040, "[%d %s:%u] debug: " msg, getpid(), __FILE__, __LINE__, ## __VA_ARGS__)
#define DEBUGSRVC(msg, ...) append_message(0x0040, msg, ## __VA_ARGS__)

int fetch_request(struct sockaddr_in*, int*, int*);
int run_worker(int, int, int, int, struct sockaddr_in*);

#endif
