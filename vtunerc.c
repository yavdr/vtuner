#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "vtuner-network.h"

#ifndef DBG_LEVEL
#define DBG_LEVEL 0x00ff
#endif

#ifndef USE_SYSLOG
#define USE_SYSLOG 1
#endif

int dbg_level =  DBG_LEVEL;
int use_syslog = USE_SYSLOG;

#define DEBUGMAIN(msg, ...)  write_message(0x0010, "[%d %s:%u] debug: " msg, getpid(), __FILE__, __LINE__, ## __VA_ARGS__)
#define DEBUGMAINC(msg, ...) write_message(0x0010, msg, ## __VA_ARGS__)

#define TS_PKT_LEN 188
#define TS_HDR_SYNC 0x47

#define MAX_DELAY 200

typedef enum application_status {
  APP_RUNNING,
  APP_EXITING,
  APP_FAILED
} application_status_t;

typedef enum tsdata_worker_status {
  DST_UNKNOWN,
  DST_RUNNING,
  DST_EXITING,
  DST_FAILED
} tsdata_worker_status_t;

typedef struct tsdata_worker_data {
  int in;
  int out;
  tsdata_worker_status_t status;  
} tsdata_worker_data_t;

application_status_t app_status;

void termination_handler(int signum) {
  app_status = APP_EXITING;
}

void *tsdata_worker(void *d) {
  tsdata_worker_data_t* data = (tsdata_worker_data_t*)d;

  data->status = DST_RUNNING;

  const int bufsiz = 697 * TS_PKT_LEN;
  unsigned char *bufptr;
  int nommap = 0;
  
  FILE *fin = NULL;
  int len;

#if 0
  struct sigaction sa = { .sa_handler = alarm_handler, .sa_flags = 0 };
  sigaction(SIGALRM, &sa, 0);
#endif

  if ((bufptr = mmap(0, bufsiz, PROT_READ | PROT_WRITE, MAP_SHARED, data->out, 0)) == MAP_FAILED)
  {
    ERROR("Memory mapping failed, using normal buffering: %m.\n");
    if ((bufptr = (char *)malloc(bufsiz)) == NULL) {
      ERROR("malloc failed - %m\n");                                                                               
      data->status = DST_FAILED;
    } else {
      nommap = 1;
    }
  }
#if 0
  alarm(1);
#endif
  if ((fin = fdopen(data->in, "r")) == NULL) {
    ERROR("fdopen failed - %m\n");
    data->status = DST_FAILED;
  } else {
    while((data->status == DST_RUNNING) && !feof(fin)) {
      if ((len = fread(bufptr, bufsiz, 1, fin)) > 0) {
          if ((len = write(data->out, bufptr, bufsiz)) != bufsiz) {
            ERROR("write failed - %m\n");
            data->status = DST_FAILED;
          }
      } else if (len != EINTR) {
        ERROR("read failed - %m\n");
        data->status = DST_FAILED;
      }
    } 
  }

#if 0
  sa.sa_handler = SIG_IGN;
  sigaction(SIGALRM, &sa, 0);
#endif

  if (nommap && bufptr) {
    free(bufptr);
  }

  ERROR("TS data copy thread terminated.\n");
  data->status = DST_EXITING;
}

typedef enum discover_worker_status {
  DWS_IDLE,
  DWS_RUNNING,
  DWS_DISCOVERD,
  DWS_FAILED
} discover_worker_status_t;

typedef struct discover_worker_data {
  __u32 types;
  struct sockaddr_in server_addr;
  discover_worker_status_t status;
  vtuner_net_message_t msg;
  char *direct_ip;
  __u32 groups;
  unsigned short port;
} discover_worker_data_t;

int *discover_worker(void *d) {
  discover_worker_data_t* data = (discover_worker_data_t*)d;

  INFO("starting discover thread\n");
  data->status = DWS_RUNNING;
  int discover_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int broadcast = -1;
  if(!data->direct_ip)
    setsockopt(discover_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)); 
  
  struct sockaddr_in discover_addr;
  memset(&discover_addr, 0, sizeof(discover_addr));
  discover_addr.sin_family = AF_INET;
  discover_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  discover_addr.sin_port = 0;

  if (bind(discover_fd, (struct sockaddr *) &discover_addr, sizeof(discover_addr)) < 0) {
    ERROR("can't bind discover socket - %m\n");
    close(discover_fd);
    data->status = DWS_FAILED;
    goto discover_worker_end;
  } 

  struct sockaddr_in  msg_addr;
  msg_addr.sin_family = AF_INET;
  if(data->direct_ip) {
    struct in_addr dirip;
    if(!inet_aton(data->direct_ip, &msg_addr.sin_addr)) {
      ERROR("can't parse direct ip\n");
      close(discover_fd);
      data->status = DWS_FAILED;
      goto discover_worker_end;
    }
  } else
    msg_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  msg_addr.sin_port = htons(data->port);

  memset(&data->msg, 0, sizeof(data->msg));
  data->msg.msg_type = MSG_DISCOVER;
  data->msg.u.discover.vtype = data->types;
  data->msg.u.discover.port = 0;
  hton_vtuner_net_message(&data->msg, 0); // we don't care tuner type for conversion of discover

  int timeo = 300;
  do {
    INFO("Sending %sdiscover message for device types %x\n", data->direct_ip ? "direct " : "", data->types);
    sendto(discover_fd, &data->msg, sizeof(data->msg), 0, (struct sockaddr *) &msg_addr, sizeof(msg_addr));
    struct pollfd pfd[] = { { discover_fd, POLLIN, 0 } }; 
    while( data->msg.u.discover.port == 0 &&
           poll(pfd, 1, timeo) == 1 ) {
      int server_addrlen = sizeof(data->server_addr);
      recvfrom( discover_fd,  &data->msg, sizeof(data->msg), 0, (struct sockaddr *)&data->server_addr, &server_addrlen );
    }
    if( timeo < 10000) timeo *= 2;
  } while (data->msg.u.discover.port == 0); 

  data->server_addr.sin_port = data->msg.u.discover.port; // no need for ntoh here
  ntoh_vtuner_net_message(&data->msg, 0);
  
  INFO("Received discover message from %s control %d data %d\n", inet_ntoa(data->server_addr.sin_addr), data->msg.u.discover.port, data->msg.u.discover.tsdata_port);
  data->status = DWS_DISCOVERD;

discover_worker_end:
  close(discover_fd);
}

typedef enum vtuner_status {
  VTS_DISCONNECTED,
  VTS_DISCOVERING,
  VTS_CONNECTED
} vtuner_status_t;

// dvb_frontend_info for DVB-S2
struct dvb_frontend_info fe_info_dvbs2 = {
  .name                  = "vTuner DVB-S2",
  .type                  = 0,
  .frequency_min         = 925000,
  .frequency_max         = 2175000,
  .frequency_stepsize    = 125000,
  .frequency_tolerance   = 0,
  .symbol_rate_min       = 1000000,
  .symbol_rate_max       = 45000000,
  .symbol_rate_tolerance = 0,
  .notifier_delay        = 0,
  .caps                  = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 | FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO | FE_CAN_QPSK | FE_CAN_RECOVER //0x400006ff
};


struct dvb_frontend_info fe_info_dvbs = {
  .name                  = "vTuner DVB-S",
  .type                  = 0,
  .frequency_min         = 950000,
  .frequency_max         = 2150000,
  .frequency_stepsize    = 125,
  .frequency_tolerance   = 0,
  .symbol_rate_min       = 1000000,
  .symbol_rate_max       = 45000000,
  .symbol_rate_tolerance = 500,
  .notifier_delay        = 0,
  .caps                  = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO | FE_CAN_QPSK //0x6af
};

struct dvb_frontend_info fe_info_dvbc = {
  .name                  = "vTuner DVB-C",
  .type                  = 1,
  .frequency_min         = 55000000,
  .frequency_max         = 862000000,
  .frequency_stepsize    = 0,
  .frequency_tolerance   = 0,
  .symbol_rate_min       = 451875,
  .symbol_rate_max       = 7230000,
  .symbol_rate_tolerance = 0,
  .notifier_delay        = 0,
  .caps                  = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO | FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 | FE_CAN_QAM_128 | FE_CAN_QAM_256 | FE_CAN_RECOVER
};

struct dvb_frontend_info fe_info_dvbt = {
  .name                  = "vTuner DVB-T",
  .type                  = 2,
  .frequency_min         = 51000000,
  .frequency_max         = 858000000,
  .frequency_stepsize    = 166667,
  .frequency_tolerance   = 0,
  .symbol_rate_min       = 0,
  .symbol_rate_max       = 0,
  .symbol_rate_tolerance = 0,
  .notifier_delay        = 0,
  .caps                  = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO | FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO | FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO //0xb2eaf
};

#define MAX_NUM_VTUNER_MODES 3

int main(int argc, char **argv) {

  openlog("vtunerc", LOG_PERROR, LOG_USER);

  int modes, mode;
  char ctypes[MAX_NUM_VTUNER_MODES][32] = {"none", "none", "none"};
  struct dvb_frontend_info* vtuner_info[MAX_NUM_VTUNER_MODES];
  int types[MAX_NUM_VTUNER_MODES];
  char *pext, *direct_ip = NULL;
  unsigned int groups = 0xFFFFFFFF; // means 'ANY group'
  unsigned int discover_port = VTUNER_DISCOVER_PORT;
  int argadd = 0;
  int vtuner_control;
  struct stat st;
  char *ctrl_devname = VTUNER_CTRL_DEVNAME;
  struct sigaction new_action, old_action;
     

  app_status = APP_RUNNING;

  /* Set up the structure to specify the new action. */
  new_action.sa_handler = termination_handler;
  sigemptyset (&new_action.sa_mask);
  new_action.sa_flags = 0;
     
  sigaction (SIGINT, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    sigaction (SIGINT, &new_action, NULL);
  sigaction (SIGHUP, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    sigaction (SIGHUP, &new_action, NULL);
  sigaction (SIGTERM, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    sigaction (SIGTERM, &new_action, NULL);

  // first option tuple can be control device name (ie: -d /dev/vtunerc0)
  if(argc > 2 && !strcmp(argv[1],"-d")) {
    if(!stat(argv[2], &st) && S_ISCHR(st.st_mode)) {
      ctrl_devname = argv[2];
      argadd = 2;
    } else {
      ERROR("can't open control device: %s\n", argv[2]);
      exit(1);
    }
  }

  vtuner_control = open(ctrl_devname, O_RDWR);
  if (vtuner_control < 0) {
    perror(ctrl_devname);
    return 1;
  }

  if (ioctl(vtuner_control, VTUNER_SET_NAME, "vTuner"))  {
    ERROR("VTUNER_SET_NAME failed - %m\n");
    exit(1);
  }

#ifdef HAVE_DREAMBOX_HARDWARE
  if (ioctl(vtuner_control, VTUNER_SET_HAS_OUTPUTS, "no")) {
    ERROR("VTUNER_SET_HAS_OUTPUTS failed - %m\n");
    exit(1);
  }
#endif

  mode = 0;
  if(strstr(argv[0],"vtunercs") != NULL) {
    types[0] = VT_S|VT_S2; modes = 1;
    strncpy(ctypes[0],"DVB-S2",sizeof(ctypes[0]));
    vtuner_info[0] = &fe_info_dvbs2;
  } else if(strstr(argv[0],"vtunerct") != NULL ) {
    types[0] = VT_T; modes = 1;
    strncpy(ctypes[0],"DVB-T",sizeof(ctypes[0]));
    vtuner_info[0] = &fe_info_dvbt; 
  } else if(strstr(argv[0],"vtunercc") != NULL ) {
    types[0] = VT_C; modes = 1;
    strncpy(ctypes[0],"DVB-C",sizeof(ctypes[0]));
    vtuner_info[0] = &fe_info_dvbc;
  } else {
    int i; modes = 0;
    if( argc-1 > MAX_NUM_VTUNER_MODES) {
      ERROR("more than %i modes given\n", MAX_NUM_VTUNER_MODES);
      exit(1);
    }
    for(i=1+argadd; i<argc; ++i) {
      if(strncmp(argv[i],"-s2",strlen("-s2"))==0) {
        types[modes] = VT_S | VT_S2;
        vtuner_info[modes] = &fe_info_dvbs2;
        strncpy(ctypes[modes],"DVB-S2",sizeof(ctypes[0]));
      } else if(strncmp(argv[i],"-S2",strlen("-S2"))==0) {
        types[modes] = VT_S2;
        vtuner_info[modes] = &fe_info_dvbs2;
        strncpy(ctypes[modes],"DVB-S2",sizeof(ctypes[0]));
      } else if(strncmp(argv[i],"-s",strlen("-s"))==0) {
	types[modes] = VT_S | VT_S2;
        vtuner_info[modes] = &fe_info_dvbs;
        strncpy(ctypes[modes],"DVB-S",sizeof(ctypes[0]));
      } else if(strncmp(argv[i],"-S",strlen("-S"))==0) {
        types[modes] = VT_S;
        vtuner_info[modes] = &fe_info_dvbs;
        strncpy(ctypes[modes],"DVB-S",sizeof(ctypes[0]));
      } else if(strncmp(argv[i],"-c",strlen("-c"))==0) {
        types[modes] = VT_C;
        vtuner_info[modes] = &fe_info_dvbc;
        strncpy(ctypes[modes],"DVB-C",sizeof(ctypes[0]));
      } else if(strncmp(argv[i],"-t",strlen("-t"))==0) {
        types[modes] = VT_T;
        vtuner_info[modes] = &fe_info_dvbt;
        strncpy(ctypes[modes],"DVB-T",sizeof(ctypes[0]));
      } else {
        ERROR("unknown tuner mode specified: %s allow values are: -s -S -s2 -S2 -c -t\n", argv[i]);
        exit(1);
      }
      DEBUGMAIN("added frontend mode %s as mode %d, searching for tuner types %x\n", ctypes[modes], modes, types[modes]);

      // -S2:192.168.1.11:3333:0020 or -S2:192.168.1.11 or -S2:192.168.1.11:3333 or -S2:::0020
      if((pext = strchr(argv[i],':'))) {
        char *pport;
	if((pport = strchr(pext+1,':'))) {
          int nvals;
          *pport = '\0';
	  if(strlen(pext+1))
            direct_ip = strdup(pext+1);
	  // num
	  if(strlen(pport+1) & pport[1] != ':')
	    sscanf(pport+1,"%d:%x",&discover_port,&groups);
          else
	    sscanf(pport+2,"%x",&groups);
	}
        DEBUGMAIN("extra connection params: ip='%s' port=%d groups=%x\n", direct_ip ? : "<null>", discover_port, groups);
      }

      ++modes;
    }

  }
  INFO("Simulating a %s tuner\n", ctypes[mode]); 

#ifdef HAVE_DREAMBOX_HARDWARE
  int f;
  f = open("/proc/stb/info/model",O_RDONLY);
  if(f>0) {

    char model[20];
    int len;
    len = read(f, &model, sizeof(model)-1); 
    close(f);
    model[len] = 0;
    INFO("Box is a %s", model);
  }
#endif

  discover_worker_data_t dsd;
  dsd.status = DWS_IDLE;
  dsd.direct_ip = direct_ip;
  dsd.groups = groups;
  dsd.port = discover_port;
  vtuner_status_t vts = VTS_DISCONNECTED;
  long values_received = 0;
  vtuner_update_t values;
  int vfd;
  pthread_t dwt, dst;
  tsdata_worker_data_t dwd;

  #define RECORDLEN 5
  vtuner_net_message_t record[RECORDLEN]; // SET_TONE, SET_VOLTAGE, SEND_DISEQC_MSG, MSG_PIDLIST

  memset(&record, 0, sizeof(vtuner_net_message_t)*RECORDLEN);

  while((app_status == APP_RUNNING) || (dsd.status != DWS_FAILED)) {
    vtuner_net_message_t msg;

    if( vts == VTS_DISCONNECTED ) {
      DEBUGMAIN("no server connected. discover thread is %d (DWS_IDLE:%d, DWS_RUNNING:%d)\n", dsd.status, DWS_IDLE, DWS_RUNNING);
      if( dsd.status == DWS_IDLE ) {
        DEBUGMAIN("changeing frontend mode to %s\n", ctypes[mode]);
	if( modes == 1 ) {
          if (ioctl(vtuner_control, VTUNER_SET_TYPE, ctypes[0])) {
            ERROR("VTUNER_SET_TYPE failed - %m\n");
            app_status = APP_FAILED;
            break;
          }
	} else {
          if( ioctl(vtuner_control, VTUNER_SET_NUM_MODES, modes) ) {
            ERROR("VTUNER_SET_NUM_MODES( %d ) failed - %m\n", modes);
            app_status = APP_FAILED;
            break;
          }
          if( ioctl(vtuner_control, VTUNER_SET_MODES, ctypes) ) {
            ERROR(" VTUNER_SET_MODES failed( %s, %s, %s ) - %m\n", ctypes[0], ctypes[1], ctypes[2]);
            app_status = APP_FAILED;
            break;
          }
	}

        if (ioctl(vtuner_control, VTUNER_SET_FE_INFO, vtuner_info[mode])) {
          ERROR("VTUNER_SET_FE_INFO failed - %m\n");
          app_status = APP_FAILED;
          break;
        }

        DEBUGMAIN("Start discover worker for device type %x\n", types[mode]);
        dsd.types = types[mode];
        dsd.status = DWS_RUNNING;
        pthread_create( &dst, NULL, discover_worker, &dsd);
        vts = VTS_DISCOVERING;
      }
    }

    if( vts == VTS_DISCOVERING ) {
      if(dsd.status == DWS_DISCOVERD) {

        // pthread_join(&dst, NULL); // wait till discover threads finish, if hasn't
        dsd.status = DWS_IDLE;    // now it's sure to be idle

        vfd = socket(PF_INET, SOCK_STREAM, 0);
        if(vfd<0) {
          ERROR("Can't create server message socket - %m\n");
          app_status = APP_FAILED;
          break;
        }
        
        INFO("connect control socket to %s:%d\n", inet_ntoa(dsd.server_addr.sin_addr), ntohs(dsd.server_addr.sin_port));
        if(connect(vfd, (struct sockaddr *)&dsd.server_addr, sizeof(dsd.server_addr)) < 0) {
          ERROR("Can't connect to server control socket - %m\n");
          vts = VTS_DISCONNECTED;
          close(vfd);

        } else {
          
          dsd.server_addr.sin_port = htons(dsd.msg.u.discover.tsdata_port);
          dwd.in = socket(PF_INET, SOCK_STREAM, 0);
          INFO("connect data socket to %s:%d\n", inet_ntoa(dsd.server_addr.sin_addr), ntohs(dsd.server_addr.sin_port));
          if( connect(dwd.in, (struct sockaddr *)&dsd.server_addr, sizeof(dsd.server_addr)) < 0) {
            ERROR("Can't connect to server data socket -%m\n");
            close(vfd);
            close(dwd.in);
            vts = VTS_DISCONNECTED;
          } else {

            dwd.out = vtuner_control;
            dwd.status = DST_UNKNOWN; 
            pthread_create( &dwt, NULL, tsdata_worker, &dwd ); 
            vts = VTS_CONNECTED;

            // send null message to fully open connection;
            msg.msg_type = MSG_NULL;
            hton_vtuner_net_message( &msg, types[mode] );
            write(vfd, &msg, sizeof(msg));
            read(vfd, &msg, sizeof(msg));

            int i;
            for(i=0; i<RECORDLEN; ++i) {
              if(record[i].msg_type != 0) {
                memcpy(&msg, &record[i], sizeof(msg));
                hton_vtuner_net_message( &msg, types[mode] );
                if(write(vfd, &msg, sizeof(msg))>0) {
                  INFO("replay message %d\n", i);
                  if(record[i].msg_type != MSG_PIDLIST) {
                    if(read(vfd, &msg, sizeof(msg))>0) {
                      INFO("got response for message %d\n", i);
                    }
                  }
                }
              }
            }
            
          }
        }

      }
    }

    int nrp = 2;
    struct pollfd pfd[] = { { vtuner_control, POLLPRI, 0 }, {vfd, POLLIN, 0} };
    if( vts != VTS_CONNECTED) nrp = 1; // don't poll for vfd if not connected
    poll(pfd, nrp, 100); // don't poll forever cause of status changes can happen

    if(pfd[0].revents & POLLPRI) {
      DEBUGMAIN("vtuner message!\n");
      if (ioctl(vtuner_control, VTUNER_GET_MESSAGE, &msg.u.vtuner)) {
        ERROR("VTUNER_GET_MESSAGE- %m\n");
        app_status = APP_FAILED;
        break;
      }
      // we need to save to msg_type here as hton works in place
      // so it's not save to access msg_type afterwards
      int msg_type = msg.msg_type = msg.u.vtuner.type;

      if(msg_type == MSG_TYPE_CHANGED) {
        // this msg isn't forwarded to the server, instead
        // we have to change the frontend type and discover
        // another server on the network

        mode = msg.u.vtuner.body.type_changed;
        msg.u.vtuner.type = 0;
        if (ioctl(vtuner_control, VTUNER_SET_RESPONSE, &msg.u.vtuner)) {
          ERROR("VTUNER_SET_RESPONSE - %m\n");
          app_status = APP_FAILED;
          break;
        }
        // empty all recorded reconnecion information
        memset(&record, 0, sizeof(vtuner_net_message_t)*RECORDLEN);

	// disconnect from server
        vts = VTS_DISCONNECTED;
        close(vfd);

        // if disvoery is ongoing cancle it
        if(dsd.status == DWS_RUNNING) {
          DEBUGMAIN("discover thread is running, try to cancle it.\n");
          pthread_cancel(dst);
          pthread_join(dst, NULL);	  
          DEBUGMAIN("discover thread terminated.\n");
          dsd.status = DWS_IDLE;
        }
        DEBUGMAIN("msg: %d completed\n", msg_type);

      } else {
        // fill the record array in the correct order
        int recordnr = -1;
        switch(msg.u.vtuner.type) {
          case MSG_SET_FRONTEND:     recordnr=3; break;
          case MSG_SET_TONE:         recordnr=1; break;
          case MSG_SET_VOLTAGE:      recordnr=2; break;
          case MSG_SEND_DISEQC_MSG:  recordnr=0; break;
          case MSG_PIDLIST:          recordnr=4; break;
        }

        if( recordnr != -1 ) {
           memcpy(&record[recordnr], &msg, sizeof(msg));
           // this is a "state changeing" msg, make cache expired
           values_received = 0;
        }

        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        long now=t.tv_sec*1000 + t.tv_nsec/1000000;

        int dontsend = ( now - values_received < 1000 ) && 
                       ( vts == VTS_CONNECTED ) &&
                       ( msg_type == MSG_READ_STATUS || msg_type == MSG_READ_BER || 
                         msg_type == MSG_READ_SIGNAL_STRENGTH || msg_type == MSG_READ_SNR || 
                         msg_type == MSG_READ_UCBLOCKS );

        if( vts == VTS_CONNECTED && ! dontsend )  {
          hton_vtuner_net_message( &msg, types[mode] );
          write(vfd, &msg, sizeof(msg));
        }

        if( dontsend ) {
          switch(msg_type) {
            case MSG_READ_STATUS:          msg.u.vtuner.body.status = values.status; break;
            case MSG_READ_BER:             msg.u.vtuner.body.ber    = values.ber; break;
            case MSG_READ_SIGNAL_STRENGTH: msg.u.vtuner.body.ss     = values.ss; break;
            case MSG_READ_SNR:             msg.u.vtuner.body.snr    = values.snr; break; 
            case MSG_READ_UCBLOCKS:        msg.u.vtuner.body.ucb    = values.ucb; break;
          }
          DEBUGMAIN("cached values are up to date\n");
        }

        if (msg_type != MSG_PIDLIST) {
          if( vts == VTS_CONNECTED ) {
            if( ! dontsend ) {
              read(vfd, &msg, sizeof(msg));
              ntoh_vtuner_net_message( &msg, types[mode] );
            }
          } else {
            INFO("fake server answer\n");
            switch(msg.u.vtuner.type) {
              case MSG_READ_STATUS:  
                msg.u.vtuner.body.status = 0; // tuning failed
                break; 
            }
            msg.u.vtuner.type = 0; // report success
          }
          if (ioctl(vtuner_control, VTUNER_SET_RESPONSE, &msg.u.vtuner)) {
            ERROR("VTUNER_SET_RESPONSE - %m\n");
            app_status = APP_FAILED;
            break;
          }
        }
        DEBUGMAIN("msg: %d completed\n", msg_type);
      }
    }
         
    if( pfd[1].revents & POLLIN ) {
      int rlen = read(vfd, &msg, sizeof(msg));
      if(rlen == 0) {
        ERROR("Server disconncted\n");
        vts = VTS_DISCONNECTED;        
        close(vfd);
      } 
      if( rlen == sizeof(msg)) {
        ntoh_vtuner_net_message( &msg, types[mode] );
        switch(msg.msg_type) {
          case MSG_UPDATE: {
            struct timespec t;
            clock_gettime(CLOCK_MONOTONIC, &t);
            values_received=t.tv_sec*1000 + t.tv_nsec/1000000;
            memcpy(&values, &msg.u.update, sizeof(values));
            break;
          }
        } 
      }
    }
  }

  // if discovery is ongoing cancel it
  if(dsd.status == DWS_RUNNING) {
    DEBUGMAIN("discover thread is running, try to cancle it.\n");
    pthread_cancel(dst);
    pthread_join(dst, NULL);	  
    DEBUGMAIN("discover thread terminated.\n");
    dsd.status = DWS_IDLE;
  }

  // if tsdata worker ongoing cancel it
  if(dwd.status == DWS_RUNNING) {
    DEBUGMAIN("tsdata worker thread is running, try to cancle it.\n");
    pthread_cancel(dwt);
    pthread_join(dwt, NULL);	  
    DEBUGMAIN("tsdata worker thread terminated.\n");
    dwd.status = DST_EXITING;
  }

  return 0;
}
