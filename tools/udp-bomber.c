#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

int main(int argc, char **argv) {
  if(argc < 2) {
    printf("usage %s ip\n", argv[0]);
    exit(1);
  }

  char buf[188*348];
  struct hostent *host;
  struct sockaddr_in addr;

  host = gethostbyname(argv[1]);

  addr.sin_family = AF_INET;
  memcpy((char*)&addr.sin_addr.s_addr, (char*)host->h_addr, host->h_length);
  addr.sin_port = htons(0x9988);

 int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

  while(1) {
    usleep(10000);
    sendto( sock, buf, sizeof(buf), 0, (struct sockaddr*)&addr, sizeof(addr));
 }

}
