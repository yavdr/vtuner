#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>

#include "vtunerd-service.h"

#define xstr(s) str(s)
#define str(s) #s

static unsigned short discover_port = VTUNER_DISCOVER_PORT;

int discover_fd = 0;

int init_vtuner_service() {
	struct sockaddr_in discover_so;

	memset(&discover_so, 0, sizeof(discover_so));
	discover_so.sin_family = AF_INET;
	discover_so.sin_addr.s_addr = htonl(INADDR_ANY);
	discover_so.sin_port = htons(0x9989);
	discover_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if( bind(discover_fd, (struct sockaddr *) &discover_so, sizeof(discover_so)) < 0) {
		ERROR("failed to bind autodiscover socket - %m\n");
		return 0;
	}
	DEBUGSRV("autodiscover socket bound\n");
	return 1;
}

int prepare_anon_stream_socket(struct sockaddr_in* addr, socklen_t* addrlen) {

  int listen_fd;
  int ret;

  ret = listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(ret < 0) {
    ERROR("Failed to create socket - %m\n");
    goto error;
  }

  memset((char *)addr, 0, *addrlen);
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = INADDR_ANY;
  addr->sin_port = 0;

  if( ret = bind(listen_fd, (struct sockaddr*)addr, *addrlen) < 0) {
    ERROR("failed to bind socket - %m\n");
    goto cleanup_listen;
  }

  getsockname(listen_fd, (struct sockaddr*)addr, addrlen);

  if( ret = listen(listen_fd, 1) < 0 ) {
    ERROR("failed to listen on socket - %m\n");
    goto cleanup_listen;
  }
  
  INFO("anon stream socket prepared %d\n", listen_fd);
  return(listen_fd);

cleanup_listen:
  close(listen_fd);

error:
  return(ret);
}

void set_socket_options(int fd) {
    int opt=1;
    if( setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0)
      WARN("setsockopt TCP_NODELAY %d failed -%m\n",opt);
    else
      DEBUGSRV("setsockopt TCP_NODELAY %d successful\n",opt);

    opt=1;
    if( setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0)
      WARN("setsockopt SO_KEEPALIVE %d failed -%m\n",opt);
    else
      DEBUGSRV("setsockopt SO_KEEPALIVE %d successful\n",opt);

    // keepalive interval 15;
    opt=15;
    if( setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &opt, sizeof(opt)) < 0)
      WARN("setsockopt TCP_KEEPIDLE %d failed -%m\n",opt);
    else
      DEBUGSRV("setsockopt TCP_KEEPIDLE %d successful\n",opt);

    // retry twice
    opt=2;
    if(setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &opt, sizeof(opt))  < 0)
      WARN("setsockopt TCP_KEEPCNT %d failed -%m\n",opt);
    else
      DEBUGSRV("setsockopt TCP_KEEPCNT %d successful\n",opt);

    // allow 2 sec. to answer on keep alive
    opt=2;
    if(setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &opt, sizeof(opt)) < 0)
      WARN("setsockopt TCP_KEEPINTVL %d failed -%m\n",opt);
    else
      DEBUGSRV("setsockopt TCP_KEEPINTVL %d successful\n",opt);
}

typedef enum tsdata_worker_status {
	DST_RUNNING,
	DST_EXITING,
	DST_FAILED,
	DST_ENDED
} tsdata_worker_status_t;

typedef struct tsdata_worker_data {
	int in;
	int listen_fd;
	tsdata_worker_status_t status;
} tsdata_worker_data_t;

void *tsdata_worker(void *d) {
  tsdata_worker_data_t* data = (tsdata_worker_data_t*)d;

  int out_fd;
  struct sockaddr_in addr;
  socklen_t addrlen=sizeof(addr);

  data->status = DST_RUNNING;
  DEBUGSRV("tsdata_worker thread started.\n");

  #ifdef DBGTS
  int dbg_fd=open( xstr(DBGTS) , O_RDWR|O_TRUNC);
  if(dbg_fd<0)
    DEBUGSRV("Can't open debug ts file %s - %m\n",xstr(DBGTS));
  else
    DEBUGSRV("copy TS data to %s\n", xstr(DBGTS));
  #endif

  out_fd = accept(data->listen_fd, (struct sockaddr *)&addr, &addrlen);
  if( out_fd < 0) {
    ERROR("accept failed on data socket - %m\n");
    data->status = DST_FAILED;
    goto error;
  }

  data->status = DST_RUNNING;
  close(data->listen_fd);
  data->listen_fd=0;

  set_socket_options(out_fd);
 
  #define RMAX (188*174)
  #define WMAX (4*RMAX)  // match client read size
  unsigned char buffer[WMAX*4]; 
  int bufptr = 0, bufptr_write = 0;

  size_t window_size = sizeof(buffer);
  if(setsockopt(out_fd, SOL_SOCKET, SO_SNDBUF, (char *) &window_size, sizeof(window_size))) {
    WARN("set window size failed - %m\n");
  }

  if( fcntl(out_fd, F_SETFL, O_NONBLOCK) != 0) {
    WARN("O_NONBLOCK failed for socket - %m\n");
  }

  long long now, last_written;
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  last_written = (long long)t.tv_sec*1000 + (long long)t.tv_nsec/1000000;

  while(data->status == DST_RUNNING) {
    struct pollfd pfd[] = { {data->in, POLLIN, 0}, {out_fd, POLLOUT, 0} };
    int waiting = poll(pfd, 2, 10);

    if(pfd[0].revents & POLLIN) {
      int rmax = (sizeof(buffer) - bufptr);
      // reading too much data can delay writing, read 1/4
      // of RMAX
      rmax = (rmax>RMAX)?RMAX:rmax;
      if(rmax == 0) {
        INFO("no space left in buffer to read data, data loss possible\n");
      } else {
        int rlen = read(data->in, buffer + bufptr, rmax);
        if(rlen>0) bufptr += rlen;

        DEBUGSRV("receive buffer stats size:%d(%d,%d), read:%d(%d,%d)\n", \
                  rmax, rmax/188, rmax%188,
                  rlen, rlen/188, rlen%188); 

      }
    } 

    int w = bufptr - bufptr_write;
    clock_gettime(CLOCK_MONOTONIC, &t);
    now = (long long)t.tv_sec*1000 + (long long)t.tv_nsec/1000000;
    long long delta = now - last_written;

    // 2010-04-04:
    // send data in the same amount as received on the
    // client side, this should reduce syscalls on both
    // ends of the connection
    if( (pfd[1].revents & POLLOUT) && \
        (w >= WMAX || (now - last_written > 100 && w > 0)) ) {
      w = w>WMAX?WMAX:w; // write the same amount of data the client prefers to read
      int wlen = write(out_fd,  buffer + bufptr_write, w);
/*
      if(delta>100) {
        INFO("data sent late: size:%d, written:%d, delay: %lld\n", \
              bufptr - bufptr_write, wlen, delta);
      }
*/
      #ifdef DBGTS
      int dgblen = write(dbg_fd, buffer + bufptr_write, w);
      if( wlen != dgblen) {
        DEBUGSRV("stream write:%d debug file write:%d. debug file me be corrupt.\n");
      }
      #endif
      
      if(wlen>=0) {
        bufptr_write += wlen;
        // 2010-01-30 do not reset on each write
        // last_written = now;
      } else {
        if( errno != EAGAIN ) {
          data->status = DST_FAILED;
          ERROR("stream write failed %d!=%d - %m\n", errno, EAGAIN);
        }
      }
      if (bufptr_write == bufptr) {
        bufptr_write = bufptr = 0;
        // 2010-01-30 reset last_writen only if buffer is empty
        last_written = now;
      }
    } else {
      // 2010-01-30
      // if nothing is written, wait a few ms to avoid reading
      // data in small chunks, max. read chunk is ~128kB
      // 20ms wait. should give ~6.4MB/s 
      usleep(20*1000);
    }
  }

error:
  INFO("TS data copy thread terminated.\n");
  data->status = DST_ENDED;
}

int fetch_request(struct sockaddr_in *client_so, int *tuner_type, int *tuner_group) {

	int clientlen = sizeof(*client_so);
	vtuner_net_message_t msg;

	if(discover_fd==0) {
		if( ! init_vtuner_service()) return 0;
	}

	INFO("waiting for autodiscover packet ...\n");
	if( recvfrom(discover_fd, &msg, sizeof(msg), 0, (struct sockaddr *) client_so, &clientlen) >0 ) {
		ntoh_vtuner_net_message(&msg, 0); // we don't care frontend type
		*tuner_type = msg.u.discover.vtype;
		DEBUGSRV("request received\n");
	} else {
		return 0;
	}

	return 1;
}

int run_worker(int adapter, int fe, int demux, int dvr, struct sockaddr_in *client_so) {

	vtuner_net_message_t msg;
	vtuner_hw_t hw;

	struct sockaddr_in ctrl_so, data_so;
	socklen_t ctrllen = sizeof(ctrl_so), datalen = sizeof(data_so);

	int listen_fd, ctrl_fd;
	int ex = 0;

	#if HAVE_DVB_API_VERSION < 3
		FrontendParameters fe_params;
	#else
		struct dvb_frontend_parameters fe_params;
	#endif

	if( ! hw_init(&hw, adapter, fe, demux, dvr)) {
		ERROR("hardware init failed\n");
		goto cleanup_hw;
	}

	listen_fd = prepare_anon_stream_socket( &ctrl_so, &ctrllen);
	if( listen_fd < 0) {
		ERROR("control socket init failed\n");
		goto cleanup_hw;
	}

	msg.u.discover.port = ntohs(ctrl_so.sin_port);
	INFO("control socket bound to %d\n", msg.u.discover.port);

	tsdata_worker_data_t dwd;
	dwd.in = hw.streaming_fd;
	dwd.listen_fd =  prepare_anon_stream_socket( &data_so, &datalen);
	if( dwd.listen_fd < 0)
		goto cleanup_listen;
	dwd.status = DST_RUNNING;
	pthread_t dwt;
	pthread_create( &dwt, NULL, tsdata_worker, &dwd );

	msg.u.discover.tsdata_port = ntohs(data_so.sin_port);
	msg.msg_type = MSG_DISCOVER;
	INFO("session prepared control:%d data:%d\n", msg.u.discover.port, msg.u.discover.tsdata_port);
    hton_vtuner_net_message(&msg, 0);
    if(sendto(discover_fd, &msg, sizeof(msg), 0, (struct sockaddr *)client_so, sizeof(*client_so))>0)
    	DEBUGSRV("Answered discover request\n");
    else {
    	ERROR("Failed to sent discover packet - %m\n");
    	goto cleanup_worker_thread;
    }


    struct pollfd afd[] = { { listen_fd, POLLIN, 0 } };
    if(poll(afd,1,5000)) {
    	ctrl_fd = accept(listen_fd, (struct sockaddr *)&ctrl_so, &ctrllen);
    	if(ctrl_fd<0) {
    		ERROR("accept failed on control socket - %m\n");
    		goto cleanup_worker_thread;
    	}
    } else {
    	INFO("no client connected. timeout\n");
    	goto cleanup_worker_thread;
    }

	close(listen_fd);
	listen_fd=0;

	set_socket_options(ctrl_fd);

	// client sends SET_FRONTEND with invalid data after a SET_PROPERTY
	int skip_set_frontend = 0;

	INFO("session running\n");
	while(dwd.status == DST_RUNNING) {
		struct pollfd pfd[] = { { ctrl_fd, POLLIN, 0 } };
		poll(pfd, 1, 750);
		if(pfd[0].revents & POLLIN) {
			DEBUGSRV("control message received\n");
			int rlen = read(ctrl_fd, &msg, sizeof(msg));
			if(rlen<=0) goto cleanup_worker_thread;
			int ret=0;
			if( sizeof(msg) == rlen) {
				ntoh_vtuner_net_message(&msg, hw.type);
				if(msg.msg_type < 1023 ) {
					switch (msg.u.vtuner.type) {
						case MSG_SET_FRONTEND:
							get_dvb_frontend_parameters( &fe_params, &msg.u.vtuner, hw.type);
							if( skip_set_frontend ) {
								ret = 0; // fake successful call
								DEBUGSRV("MSG_SET_FRONTEND skipped %d\n", skip_set_frontend);
							} else {
								ret=hw_set_frontend(&hw, &fe_params);
								DEBUGSRV("MSG_SET_FRONTEND %d\n", skip_set_frontend);
							}
							break;
						case MSG_GET_FRONTEND:
							ret=hw_get_frontend(&hw, &fe_params);
							set_dvb_frontend_parameters(&msg.u.vtuner, &fe_params, hw.type);
							DEBUGSRV("MSG_GET_FRONTEND\n");
							break;
						case MSG_READ_STATUS:
							ret=hw_read_status(&hw, &msg.u.vtuner.body.status);
							DEBUGSRV("MSG_READ_STATUS: 0x%x\n", msg.u.vtuner.body.status);
							break;
						case MSG_READ_BER:
							ret=ioctl(hw.frontend_fd, FE_READ_BER, &msg.u.vtuner.body.ber);
							DEBUGSRV("MSG_READ_BER: %d\n", msg.u.vtuner.body.ber);
							break;
						case MSG_READ_SIGNAL_STRENGTH:
							ret=ioctl(hw.frontend_fd, FE_READ_SIGNAL_STRENGTH, &msg.u.vtuner.body.ss);
							DEBUGSRV("MSG_READ_SIGNAL_STRENGTH: %d\n", msg.u.vtuner.body.ss);
							break;
						case MSG_READ_SNR:
							ret=ioctl(hw.frontend_fd, FE_READ_SNR, &msg.u.vtuner.body.snr);
							DEBUGSRV("MSG_READ_SNR: %d\n", msg.u.vtuner.body.snr);
							break;
						case MSG_READ_UCBLOCKS:
							ioctl(hw.frontend_fd, FE_READ_UNCORRECTED_BLOCKS, &msg.u.vtuner.body.ucb);
							DEBUGSRV("MSG_READ_UCBLOCKS %d\n", msg.u.vtuner.body.ucb);
							break;
						case MSG_SET_TONE:
							ret=hw_set_tone(&hw, msg.u.vtuner.body.tone);
							DEBUGSRV("MSG_SET_TONE: 0x%x\n", msg.u.vtuner.body.tone);
							break;
						case MSG_SET_VOLTAGE:
							ret=hw_set_voltage(&hw, msg.u.vtuner.body.voltage);
							DEBUGSRV("MSG_SET_VOLTAGE: 0x%x\n", msg.u.vtuner.body.voltage);
							break;
						case MSG_ENABLE_HIGH_VOLTAGE:
							//FIXME: need to know how information is passed to client
							WARN("MSG_ENABLE_HIGH_VOLTAGE is not implemented: %d\n", msg.u.vtuner.body.pad[0]);
							break;
						case MSG_SEND_DISEQC_MSG: {
							ret=hw_send_diseq_msg(&hw, &msg.u.vtuner.body.diseqc_master_cmd);
							DEBUGSRV("MSG_SEND_DISEQC_MSG: \n");
							break;
						}
						case MSG_SEND_DISEQC_BURST: {
							ret=hw_send_diseq_burst(&hw, msg.u.vtuner.body.burst);
							DEBUGSRV("MSG_SEND_DISEQC_BURST: %d %d\n", msg.u.vtuner.body.burst,ret);
							break;
						}
						case MSG_SET_PROPERTY:
							ret=hw_set_property(&hw, &msg.u.vtuner.body.prop);
							// in case the call was successful, we have to skip
							// the next call to SET_FRONTEND
							skip_set_frontend = (ret == 0);
							DEBUGSRV("MSG_SET_PROPERTY: %d %d %d\n", msg.u.vtuner.body.prop.cmd, skip_set_frontend, ret);
							break;
						case MSG_GET_PROPERTY:
							ret=hw_get_property(&hw, &msg.u.vtuner.body.prop);
							DEBUGSRV("MSG_GET_PROPERTY: %d %d\n", msg.u.vtuner.body.prop.cmd,ret);
							break;
						case MSG_PIDLIST:
							ret=hw_pidlist(&hw, msg.u.vtuner.body.pidlist );
							break;
						default:
							ret = 0;
							WARN("unknown vtuner message %d\n", msg.u.vtuner.type);
							// don't stop here, instead send message
							// back to avoid client hang
					}

					if (msg.u.vtuner.type != MSG_PIDLIST ) {
						if( ret!= 0 )
							WARN("vtuner call failed, type:%d reason:%d\n", msg.u.vtuner.type, ret);
						msg.u.vtuner.type = ret;
						hton_vtuner_net_message(&msg, hw.type);
						write(ctrl_fd, &msg, sizeof(msg));
					}
				} else {
					switch (msg.msg_type) {
						case MSG_NULL:
							ret = 0;
							break;
						default:
							ret = 0;
							WARN("received out-of-state control message: %d\n", msg.msg_type);
							// don't stop here, instead send message
							// back to avoid client hang
					}
					msg.u.vtuner.type = ret;
					hton_vtuner_net_message(&msg, hw.type);
					write(ctrl_fd, &msg, sizeof(msg));
				}
			} else {
				WARN("message size is invalid: %d\n", rlen);
			}
		}
	}
	ex= 1;

	cleanup_worker_thread:
		dwd.status = DST_EXITING;
		// FIXME: need a better way to know if thread has finished
		DEBUGSRV("wait for TS data copy thread to terminate\n");
		pthread_join(dwt, NULL);
		DEBUGSRV("TS data copy thread terminated - %m\n");

	cleanup_ctrl:
		close(ctrl_fd);

	cleanup_listen_data:
		close(dwd.listen_fd);

	cleanup_listen:
		close(listen_fd);

	cleanup_hw:
		hw_free(&hw);

	error:
		INFO("control thread terminated.\n");

	return ex;
}

