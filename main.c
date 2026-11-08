#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/signal.h>
#include <sys/epoll.h>
#include <fcntl.h>

#define PORT 6379
#define NUM_EVENTS 10

volatile sig_atomic_t running = 1;

void handle_shutdown(int signal)
{
  running = 0;
}

static void close_client(int epoll_fd, int fd)
{
  epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
  close(fd);
}

int main(void)
{
  signal(SIGINT, handle_shutdown);
  signal (SIGTERM, handle_shutdown);

  // closes the fd when exec is called
  int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

  struct sockaddr_in client_address;
  socklen_t client_address_len;

  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1)
  {
    printf("Socket creation failed: %s\n", strerror(errno));
    close (epoll_fd);
    return 1;
  }
  int flags = fcntl(server_socket, F_GETFL, 0); // get current flags
  if (flags == -1 || fcntl(server_socket, F_SETFL, flags | O_NONBLOCK) == -1) // add nonblock flag
  {
    perror("fcntl");
    close (epoll_fd);
    close(server_socket);
    return 1;
  }

  int reuse = 1;
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
    close (epoll_fd);
    close(server_socket);
		return 1;
	}

  struct epoll_event event;
  event.events = EPOLLIN;
  event.data.fd = server_socket;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &event) != 0)
  {
    printf("epoll_ctl failed: %s \n", strerror(errno));
    close (epoll_fd);
    close(server_socket);
		return 1;
  }

  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(PORT);
  server_address.sin_addr.s_addr = INADDR_ANY;

  if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == -1)
  {
    printf("Bind failed: %s\n", strerror(errno));
    close (epoll_fd);
    close(server_socket);
    return 1;
  }

  if (listen(server_socket, 1) == -1)
  {
    printf("Listen failed: %s\n", strerror(errno));
    close (epoll_fd);
    close(server_socket);
    return 1;
  }

  printf("Waiting for a client to connect\n");
  client_address_len = sizeof(client_address);

  while (running)
  {
    struct epoll_event events[NUM_EVENTS];
    int num_fd = epoll_wait(epoll_fd, events, NUM_EVENTS, -1);
    if (num_fd == -1) // when you press ctrl c the signal handler sets running 0 but eopll_wait may return -1 with eintr, now the while can terminate normally
    {
      if (errno == EINTR)
      {
        continue;
      }
      printf("epoll_wait interrupted: %s\n", strerror(errno));
      break;
    }
    for (size_t i = 0; i < num_fd; i++)
    {
      int fd = events[i].data.fd;
      uint32_t flags = events[i].events;
      if (fd == server_socket)
      {
        int client_socket = accept(server_socket, (struct sockaddr*)&client_address, &client_address_len);
        if (client_socket == -1)
        {
          if (errno == EAGAIN || errno == EWOULDBLOCK)
          {
            continue;;
          }
          else if (errno == EINTR)
          {
            continue;
          }
          printf("Accept failed: %s\n", strerror(errno));
          break;
        }
        int flags = fcntl(client_socket, F_GETFL, 0); // get current flags
        if (flags == -1 || fcntl(client_socket, F_SETFL, flags | O_NONBLOCK) == -1) // add nonblock flag
        {
          perror("fcntl");
          close(client_socket);
        }
        printf("Client connected\n");

        struct epoll_event new_client;
        new_client.events = EPOLLIN;
        new_client.data.fd = client_socket;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &new_client) != 0)
        {
          printf("epoll_ctl failed: %s \n", strerror(errno));
          close(client_socket);
          continue;
        }
        continue;
      }
      if (flags & EPOLLERR)
      {
        perror("Socket error on fd");
        close_client(epoll_fd, fd);
        continue;
      }
      if (flags & EPOLLIN)
      {
        char buffer[1024];
        ssize_t bytes_received = recv(events[i].data.fd, buffer, sizeof(buffer), 0);
        if (bytes_received > 0)
        {
          const char response[] = "+PONG\r\n";

          ssize_t bytes_sent = send(events[i].data.fd, response, strlen(response), 0);
          if (bytes_sent == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
          {
            printf("Sending failed %s", strerror(errno));
            close_client(epoll_fd, fd);
          }
        }
        else if (bytes_received == 0)
        {
          close_client(epoll_fd, fd);
          continue;
        }
        else if (bytes_received == -1)
        {
          if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) // again, wouldblock, intr are errnos that indicate that you should try again later
          {
            continue;
          }
          perror("recv");
          close_client(epoll_fd, fd);
          continue;
        }
      }
      else if (flags & EPOLLHUP)
      {
        close_client(epoll_fd, fd);
      }
    } 
  }



  close(server_socket);
  close(epoll_fd);

  return 0;
}