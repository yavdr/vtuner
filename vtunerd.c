#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <syslog.h>

#include "vtunerd-service.h"
#include "vtuner-utils.h"

int dbg_level  = 0x00ff;
int use_syslog = 1;

#define DEBUGMAIN(msg, ...)  write_message(0x0010, "[%d %s:%u] debug: " msg, getpid(), __FILE__, __LINE__, ## __VA_ARGS__)
#define DEBUGMAINC(msg, ...) write_message(0x0010, msg, ## __VA_ARGS__)

#define MAX_SESSIONS 8

typedef enum vtuner_session_status {
	SST_IDLE,
	SST_BUSY
} vtuner_session_status_t;

typedef struct vtuner_session {
	vtuner_session_status_t status;
	int tuner_type;
	int tuner_group;
	int adapter;
	int frontend;
	int demux;
	int dvr;
	pthread_t th;
	struct sockaddr_in client_so;
} vtuner_session_t;

void *session_worker(void *data) {
	  vtuner_session_t *session = (vtuner_session_t*)data;
	  run_worker(session->adapter, session->frontend, session->demux, session->dvr, &session->client_so);
	  session->status = SST_IDLE;
}

int main(int argc, char **argv) {
	int i;
	vtuner_session_t session[MAX_SESSIONS];

	openlog("vtunerd", LOG_PERROR, LOG_USER);

	for(i=0; i<MAX_SESSIONS; ++i) session[i].status = SST_IDLE;

	int hw_count;
	if(argc == 1) {
		session[0].adapter = 0;
		session[0].frontend = 0;
		session[0].demux = 0;
		session[0].dvr = 0;
		hw_count = 1;
	} else {
		hw_count=atoi(argv[1]);
		if( hw_count*4 + 2 != argc ) {
			ERROR("Parameter mismatch. %d tuner(s) requires %d arguments, but %d given.\n", hw_count, hw_count*4 + 1, argc-1);
			exit(2);
		}
		for(i=0;i<hw_count;++i) {
			session[i].adapter = atoi(argv[i*4+2]);
			session[i].frontend = atoi(argv[i*4+3]);
			session[i].demux = atoi(argv[i*4+4]);
			session[i].dvr = atoi(argv[i*4+5]);
			session[i].tuner_type = 0xff;

			DEBUGMAIN("register hardware adapter %d, frontend %d, demux %d, dvr %d\n",\
					session[i].adapter,session[i].frontend,session[i].demux,session[i].dvr);
		}
	}

	#if DVB_API_VERSION >= 5
		INFO("S2API tuning support.\n");
	#endif

	struct sockaddr_in client_so;
	int tuner_type, tuner_group;

	while(fetch_request(&client_so, &tuner_type, &tuner_group)) {
		INFO("received discover request\n");
		for(i=0;i<hw_count;++i)
			if(session[i].status == SST_IDLE) {
				  session[i].status = SST_BUSY;
				  memcpy((void*)&session[i].client_so, (void*)&client_so, sizeof(client_so));
				  pthread_create(&session[i].th, NULL, session_worker, (void*)&session[i]);
			}
			else
				INFO("No idle device found\n");
	}

}
