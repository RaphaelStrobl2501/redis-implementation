#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>

#define PORT 6379

int main(void)
{
  setbuf(stdout, NULL);
	setbuf(stderr, NULL);
  
  // set up server, communication via tcp sockets
  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1)
  {
    printf("Socket creation failed: %s\n", sterror(errno));
    return 1;
  }

  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(PORT);
  server_address.sin_addr.s_addr = INADDR_ANY;

  int connect_status = connect(socket_fd, (struct sockaddr*)&server_address, sizeof(server_address));
  if (connect_status == -1)
  {
    printf("Error\n");
  }
  else
  {
    char str_data[255];
    recv(socket_fd, str_data, sizeof(str_data), 0);
  }

  return 0;
}