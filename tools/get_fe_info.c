#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/dvb/frontend.h>

int main(int argc, char **argv) {

  int i;
  for(i=0; i<4; ++i) {
    char devstr[80];
    sprintf( devstr, "/dev/dvb/adapter0/frontend%d", i);

    int fe_fd = open( devstr, O_RDONLY);
    if(fe_fd>0) {
      struct dvb_frontend_info fe_info;
      if(ioctl(fe_fd, FE_GET_INFO, &fe_info) == 0) {
        printf("// dvb_frontend_info for %s\n", devstr);
        printf("struct dvb_frontend_info FETYPE = {\n");
        printf("  .name		         = \"%s\",\n", fe_info.name); 
        printf("  .type                  = %d,\n", fe_info.type);
        printf("  .frequency_min         = %d,\n", fe_info.frequency_min);
        printf("  .frequency_max         = %d,\n", fe_info.frequency_max);
        printf("  .frequency_stepsize    = %d,\n", fe_info.frequency_stepsize);
        printf("  .frequency_tolerance   = %d,\n", fe_info.frequency_tolerance);
        printf("  .symbol_rate_min       = %d,\n", fe_info.symbol_rate_min);
        printf("  .symbol_rate_max       = %d,\n", fe_info.symbol_rate_max);
        printf("  .symbol_rate_tolerance = %d,\n", fe_info.symbol_rate_tolerance);
        printf("  .notifier_delay        = %d,\n", fe_info.notifier_delay);
        printf("  .caps                  = 0x%x\n", fe_info.caps);
        printf("};\n");
      }
    }
  }
}
