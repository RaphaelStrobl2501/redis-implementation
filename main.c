#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/signal.h>

#define PORT 6379

volatile sig_atomic_t running = 1;

void handle_shutdown(int signal)
{
  running = 0;
}

int main(void)
{
  signal(SIGINT, handle_shutdown);
  signal (SIGTERM, handle_shutdown);

  struct sockaddr_in client_address;
  socklen_t client_address_len;

  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1)
  {
    printf("Socket creation failed: %s\n", strerror(errno));
    return 1;
  }

  int reuse = 1;
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}

  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(PORT);
  server_address.sin_addr.s_addr = INADDR_ANY;

  if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == -1)
  {
    printf("Bind failed: %s\n", strerror(errno));
    return 1;
  }

  if (listen(server_socket, 1) == -1)
  {
    printf("Listen failed: %s\n", strerror(errno));
    return 1;
  }

  printf("Waiting for a client to connect\n");
  client_address_len = sizeof(client_address);

  while (running)
  {
    int client_socket = accept(server_socket, (struct sockaddr*)&client_address, &client_address_len);
    if (client_socket == -1)
    {
      printf("Accept failed: %s\n", strerror(errno));
      return 1;
    }
    printf("Client connected\n");

    while (1)
    {
      char buffer[1024];
      size_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
      if (bytes_received == -1)
      {
        printf("Reading failed %s", strerror(errno));
      }

      const char response[] = "+PONG\r\n";
      size_t bytes_sent = send(client_socket, response, strlen(response), 0);
      if (bytes_sent == -1)
      {
        printf("Sending failed %s", strerror(errno));
      }
    }
    close (client_socket);
  }



  close(server_socket);

  return 0;
}