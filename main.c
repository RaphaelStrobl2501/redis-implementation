#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

int main(void)
{
  struct sockaddr_in client_address;
  int client_address_len;

  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1)
  {
    printf("Socket creation failed: %s\n", sterror(errno));
    return 1;
  }

  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(6379);
  server_address.sin_addr.s_addr = INADDR_ANY;

  if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == -1)
  {
    printf("Bind failed: %s\n", sterror(errno));
    return 1;
  }

  if (listen(server_socket, 1) == -1)
  {
    printf("Listen failed: %s\n", sterror(errno));
    return 1;
  }

  printf("Waiting for a client to connect\n");
  client_address_len = sizeof(client_address);

  if (accept(server_socket, (struct sockaddr*)&client_address, &client_address_len) == -1)
  {
    printf("Accept failed: %s\n", stderor(errno));
    return 1;
  }
  printf("Client connected\n");


  close(server_socket);

  return 0;
}