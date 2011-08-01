#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <poll.h>
#include <linux/dvb/dmx.h>

#include "vtuner-dmm-3.h"

typedef enum {
        DMX_TAP_TS = 0,
        DMX_TAP_PES = DMX_PES_OTHER, /* for backward binary compat. */
} dmx_tap_type_t;

#if DVB_API_VERSION > 3
  #define DMX_ADD_PID             _IOW('o', 51, __u16)
  #define DMX_REMOVE_PID          _IOW('o', 52, __u16)
#else
  #define DMX_ADD_PID              _IO('o', 51)
  #define DMX_REMOVE_PID           _IO('o', 52)
#endif

int hw_init(vtuner_hw_t* hw, int adapter, int frontend, int demux, int dvr) {

  char devstr[80];
  int i;

  hw->adapter = adapter;
  hw->frontend = frontend;
  hw->demux = demux;

  hw->frontend_fd = 0;
  hw->demux_fd = hw->streaming_fd = 0;
  memset(hw->pids, 0xff, sizeof(hw->pids));

  hw->num_props = 0;
  memset(hw->props, 0x00, sizeof(hw->props));

  sprintf( devstr, "/dev/dvb/adapter%d/frontend%d", hw->adapter, hw->frontend);
  hw->frontend_fd = open( devstr, O_RDWR);
  if(hw->frontend_fd < 0) {
    ERROR("failed to open %s\n", devstr);
    goto error;
  }

  if(ioctl(hw->frontend_fd, FE_GET_INFO, &hw->fe_info) != 0) {
    ERROR("FE_GET_INFO failed for %s\n", devstr);
    goto error;    
  }

  switch(hw->fe_info.type) {
    case FE_QPSK: hw->type = VT_S; break;
    case FE_QAM:  hw->type = VT_C; break;
    case FE_OFDM: hw->type = VT_T; break;
    default:
      ERROR("Unknown frontend type %d\n", hw->fe_info.type);
      goto cleanup_fe;
  }
  INFO("FE_GET_INFO dvb-type:%d vtuner-type:%d\n", hw->fe_info.type, hw->type);
  
  sprintf( devstr, "/dev/dvb/adapter%d/demux%d", hw->adapter, demux);
  hw->demux_fd = hw->streaming_fd = open(devstr, O_RDWR|O_NONBLOCK);
  if(hw->demux_fd<0) {
    ERROR("failed to open %s\n", devstr);
    goto cleanup_fe;
  }

  dmx_source_t src = DMX_SOURCE_FRONT0 + frontend;
  if( ioctl(hw->demux_fd, DMX_SET_SOURCE, &src) ) {
    ERROR("DMX_SET_SOURCE failed for %s - %m\n",devstr);
    goto cleanup_demux;
  }

  if( ioctl(hw->demux_fd, DMX_SET_BUFFER_SIZE, 1024*1024) != 0 ) {
    ERROR("DMX_SET_BUFFER_SIZE failed for %s\n",devstr);
    goto cleanup_demux;
  }
/*
  if( fcntl(hw->demux_fd, F_SETFL, O_NONBLOCK) != 0) {
    ERROR("O_NONBLOCK failed for %s\n",devstr);
    goto cleanup_demux;
  }
*/
  struct dmx_pes_filter_params flt;
  flt.input = DMX_IN_FRONTEND;
#if DVB_API_VERSION > 3
  // FIXME: no useful PID known yet, give 1 to have at least a valid
  flt.pid = 1;
  flt.output  = DMX_OUT_TSDEMUX_TAP;
  flt.pes_type = DMX_PES_OTHER;
#else
  flt.pid = -1;
  flt.output = DMX_OUT_TAP;
  flt.pes_type = DMX_TAP_TS;
#endif
  flt.flags = 0;
  if(ioctl(hw->demux_fd, DMX_SET_PES_FILTER, &flt) != 0) {
    ERROR("DMX_SET_PES_FILTER failed for %s - %m\n", devstr);
    goto cleanup_demux;
  }

  if(ioctl(hw->demux_fd, DMX_START, 0) != 0) {
    ERROR("DMX_START failed for %s - %m\n", devstr);
  } 

  return 1;

cleanup_demux:
  close(hw->demux_fd);

cleanup_fe:
  close(hw->frontend_fd);

error:
  return 0;
} 

void hw_free(vtuner_hw_t *hw) {
	if(hw->demux_fd) close(hw->demux_fd);
	if(hw->frontend_fd) close(hw->frontend_fd);
}

void print_frontend_parameters(vtuner_hw_t* hw, struct dvb_frontend_parameters* fe_params, char *msg, size_t msgsize) {
  switch(hw->type) {
    case VT_S: snprintf(msg, msgsize, "freq:%d inversion:%d SR:%d FEC:%d\n", \
                        fe_params->frequency, fe_params->inversion, \
                        fe_params->u.qpsk.symbol_rate, fe_params->u.qpsk.fec_inner); 
               break;
    case VT_C: snprintf(msg, msgsize, "freq:%d inversion:%d SR:%d FEC:%d MOD:%d\n", \
                        fe_params->frequency, fe_params->inversion, \
                        fe_params->u.qam.symbol_rate, fe_params->u.qam.fec_inner, fe_params->u.qam.modulation); 
               break;
    case VT_T: break;
  }
}

int hw_get_frontend(vtuner_hw_t* hw, struct dvb_frontend_parameters* fe_params) {
  int ret;
  ret = ioctl(hw->frontend_fd, FE_GET_FRONTEND, fe_params);
  if( ret != 0 ) WARN("FE_GET_FRONTEND failed\n");
  return ret;
}

int hw_set_frontend(vtuner_hw_t* hw, struct dvb_frontend_parameters* fe_params) {
  int ret;
  char msg[1024];
  print_frontend_parameters(hw, fe_params, msg, sizeof(msg));
  INFO("FE_SET_FRONTEND parameters: %s", msg);
  ret = ioctl(hw->frontend_fd, FE_SET_FRONTEND, fe_params);
  if( ret != 0 ) WARN("FE_SET_FRONTEND failed %s\n", msg);
  return ret;
}

int hw_get_property(vtuner_hw_t* hw, struct dtv_property* prop) {

}

int hw_set_property(vtuner_hw_t* hw, struct dtv_property* prop) {
  int ret=0;
#if DVB_API_VERSION < 5
  ret = -1;
  WARN("FE_SET_PROPERTY is not available\n");
#else
  DEBUGHW("FE_SET_PROPERTY %d\n", prop->cmd);
  switch( prop->cmd ) {
    case DTV_UNDEFINED: break;
    case DTV_CLEAR: 
      hw->num_props = 0; 
      DEBUGHW("FE_SET_PROPERTY: DTV_CLEAR\n");
      break;
    case DTV_TUNE: {
      hw->props[hw->num_props].cmd = prop->cmd;
      hw->props[hw->num_props].u.data = prop->u.data;
      ++hw->num_props;

      struct dtv_properties cmdseq;
      cmdseq.num = hw->num_props;
      cmdseq.props = hw->props;
      DEBUGHW("FE_SET_PROPERTY: DTV_TUNE\n");
      ret=ioctl(hw->frontend_fd, FE_SET_PROPERTY, &cmdseq);
      if( ret != 0 ) WARN("FE_SET_PROPERTY failed - %m\n");
    } break;
    case DTV_FREQUENCY:
    case DTV_MODULATION:
    case DTV_BANDWIDTH_HZ:
    case DTV_INVERSION:
    case DTV_DISEQC_MASTER:
    case DTV_SYMBOL_RATE:
    case DTV_INNER_FEC:
    case DTV_VOLTAGE:
    case DTV_TONE:
    case DTV_PILOT:
    case DTV_ROLLOFF:
    case DTV_DISEQC_SLAVE_REPLY:
    case DTV_FE_CAPABILITY_COUNT:
    case DTV_FE_CAPABILITY:
    case DTV_DELIVERY_SYSTEM:
      if(hw->num_props < DTV_IOCTL_MAX_MSGS) {
        hw->props[hw->num_props].cmd = prop->cmd;
        hw->props[hw->num_props].u.data = prop->u.data;
        DEBUGHW("FE_SET_PROPERTY: set %d to %d, %d properties collected\n", hw->props[hw->num_props].cmd, hw->props[hw->num_props].u.data, hw->num_props+1);
        ++hw->num_props;
      } else {
        WARN("FE_SET_PROPERTY properties limit (%d) exceeded.\n", DTV_IOCTL_MAX_MSGS);
        ret = -1;
      } break;
    default:
      WARN("FE_SET_PROPERTY unknown property %d\n", prop->cmd);
      ret = -1;
  }
#endif
  return ret;
}

int hw_read_status(vtuner_hw_t* hw, __u32* status) {
  int ret;
  ret = ioctl(hw->frontend_fd, FE_READ_STATUS, status);
  if( ret != 0 ) WARN("FE_READ_STATUS failed\n");
  return ret;
}

int hw_set_tone(vtuner_hw_t* hw, __u8 tone) {
  int ret=0;
  ret = ioctl(hw->frontend_fd, FE_SET_TONE, tone);
  if( ret != 0 ) WARN("FE_SET_TONE failed - %m\n");
  return ret;
}

int hw_set_voltage(vtuner_hw_t* hw, __u8 voltage) {
  int ret=0;
  // Dream supports this on DVB-T, but not plain linux
  ret = ioctl(hw->frontend_fd, FE_SET_VOLTAGE, voltage);
  if( ret != 0 ) WARN("FE_SET_VOLTAGE failed\n");
  return ret;
}

int hw_send_diseq_msg(vtuner_hw_t* hw, diseqc_master_cmd_t* cmd) {
  int ret=0;
  ret=ioctl(hw->frontend_fd, FE_DISEQC_SEND_MASTER_CMD, cmd);
  if( ret != 0 ) WARN("FE_DISEQC_SEND_MASTER_CMD failed - %m\n");
  return ret;
}

int hw_send_diseq_burst(vtuner_hw_t* hw, __u8 burst) {
  int ret=0;
  ret=ioctl(hw->frontend_fd, FE_DISEQC_SEND_BURST, burst);
  if( ret != 0 ) WARN("FE_DISEQC_SEND_BURST  - %m\n");
  return ret;
}

int hw_pidlist(vtuner_hw_t* hw, __u16* pidlist) {
  int i,j;

  DEBUGHWI("hw_pidlist befor: ");
  for(i=0; i<30; ++i) if(hw->pids[i] != 0xffff) DEBUGHWC("%d ", hw->pids[i]);
  DEBUGHWF("\n");
  DEBUGHWI("hw_pidlist sent:  ");
  for(i=0; i<30; ++i) if(pidlist[i] != 0xffff) DEBUGHWC("%d ", pidlist[i]);
  DEBUGHWF("\n");

  for(i=0; i<30; ++i) 
    if(hw->pids[i] != 0xffff) {
      for(j=0; j<30; ++j) 
        if(hw->pids[i] == pidlist[j])
          break;
      if(j == 30) {
        #if DVB_API_VERSION > 3
          if(ioctl(hw->demux_fd, DMX_REMOVE_PID, &hw->pids[i]) != 0) {
        #else
          if(ioctl(hw->demux_fd, DMX_REMOVE_PID, hw->pids[i]) != 0) {
        #endif
          WARN("DMX_REMOVE_PID %d failed - %m\n", hw->pids[i]);
        }
//        DEBUGHW("remove pid %d\n", hw->pids[i]);
      }
    }

  for(i=0; i<30; ++i) 
    if(pidlist[i] != 0xffff) {
      for(j=0; j<30; ++j) 
        if(pidlist[i] == hw->pids[j])
          break;
      if(j == 30) {
	#if DVB_API_VERSION > 3
          if(ioctl(hw->demux_fd, DMX_ADD_PID, &pidlist[i]) != 0) {
        #else
          if(ioctl(hw->demux_fd, DMX_ADD_PID, pidlist[i]) != 0) {
        #endif
          WARN("DMX_ADD_PID %d failed - %m\n", pidlist[i]);
        }
//        DEBUGHW("add pid %d\n",  pidlist[i]);
      }
    }

  memcpy(hw->pids, pidlist, sizeof(hw->pids));
  return 0;
}
