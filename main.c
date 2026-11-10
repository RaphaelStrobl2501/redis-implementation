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
#define MAX_ARGUMENTS 1024
#define MAX_ARGUMENT_LENGTH 1024

volatile sig_atomic_t running = 1;

typedef enum
{
  PARSE_FINISHED,
  PARSE_NEED_MORE_CHARS,
  PARSE_INVALID
} parseState;

typedef struct
{
  int fd;
  bool is_listener;
  char buffer[MAX_ARGUMENT_LENGTH + 1];
  size_t buffer_len;
} Client;

void handle_shutdown(int signal)
{
  running = 0;
}

static void close_client(int epoll_fd, Client *client)
{
  epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
  close(client->fd);
  free(client);
}

parseState parseRespString(const char *text, size_t text_len, char arguments[MAX_ARGUMENTS][MAX_ARGUMENT_LENGTH], size_t *amount_arguments)
{
  size_t index = 0;

  if (index >= text_len)
    return PARSE_NEED_MORE_CHARS;

  if (text[index++] != '*')
    return PARSE_INVALID;

  char *end; // contains address to first char not a num
  unsigned long count = strtoul(text + index, &end, 10);

  if (end == text + index)
  {
    if (index >= text_len)
      return PARSE_NEED_MORE_CHARS;

    return PARSE_INVALID;
  }

  if (count > MAX_ARGUMENTS)
    return PARSE_INVALID;

  index = end - text;

  if (index >= text_len)
    return PARSE_NEED_MORE_CHARS;

  if (text[index] != '\r' || text[index + 1] != '\n')
    return PARSE_INVALID;

  index += 2; // skip to $
  *amount_arguments = (size_t)count;

  for (size_t i = 0; i < *amount_arguments; i++)
  {
    if (index >= text_len)
      return PARSE_NEED_MORE_CHARS;
    if (text[index++] != '$')
      return PARSE_INVALID;

    unsigned long len_argument = strtoul(text + index, &end, 10);
    if (end == text + index)
    {
      if (index >= text_len)
        return PARSE_NEED_MORE_CHARS;

      return PARSE_INVALID;
    }

    if (count > MAX_ARGUMENTS)
      return PARSE_INVALID;

    index = end - text;

    if (index >= text_len)
      return PARSE_NEED_MORE_CHARS;

    if (text[index] != '\r' || text[index + 1] != '\n')
      return PARSE_INVALID;

    index += 2;

    if (len_argument > text_len - index || text_len - index - len_argument < 2)
      return PARSE_NEED_MORE_CHARS;

    memcpy(arguments[i], text + index, len_argument);
    arguments[i][len_argument] = '\0';

    index += len_argument;
    if (text[index] != '\r' || text[index + 1] != '\n')
      return PARSE_INVALID;

    index += 2;
  }
  return PARSE_FINISHED;
}

int echoCommand(Client* client, size_t amount_arguments, char arguments[MAX_ARGUMENTS][MAX_ARGUMENT_LENGTH])
{
  if (amount_arguments == 1 && strcasecmp(arguments[0], "PING") == 0)
  {
      const char *response = "+PONG\r\n";

      ssize_t bytes_sent = send(
          client->fd,
          response,
          strlen(response),
          MSG_NOSIGNAL
      );

      if (bytes_sent == -1)
          perror("send");
  }
  if (amount_arguments == 2 && strcasecmp(arguments[0], "ECHO") == 0)
  {
    size_t len = strlen(arguments[1]);

    char response[MAX_ARGUMENT_LENGTH + 32];

    int header_len = snprintf(
        response,
        sizeof(response),
        "$%zu\r\n",
        len);

    memcpy(response + header_len, arguments[1], len);
    memcpy(response + header_len + len, "\r\n", 2);

    size_t response_len = (size_t)header_len + len + 2;

    ssize_t bytes_sent = send(
        client->fd,
        response,
        response_len,
        MSG_NOSIGNAL);

    return bytes_sent;
  }
}

int main(void)
{
  signal(SIGINT, handle_shutdown);
  signal(SIGTERM, handle_shutdown);

  // closes the fd when exec is called
  int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

  struct sockaddr_in client_address;
  socklen_t client_address_len;

  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1)
  {
    printf("Socket creation failed: %s\n", strerror(errno));
    close(epoll_fd);
    return 1;
  }
  int flags = fcntl(server_socket, F_GETFL, 0);                               // get current flags
  if (flags == -1 || fcntl(server_socket, F_SETFL, flags | O_NONBLOCK) == -1) // add nonblock flag
  {
    perror("fcntl");
    close(epoll_fd);
    close(server_socket);
    return 1;
  }

  int reuse = 1;
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    printf("SO_REUSEADDR failed: %s \n", strerror(errno));
    close(epoll_fd);
    close(server_socket);
    return 1;
  }

  Client listener = {
      .fd = server_socket,
      .is_listener = true};

  struct epoll_event event;
  event.events = EPOLLIN;
  event.data.ptr = &listener;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &event) != 0)
  {
    printf("epoll_ctl failed: %s \n", strerror(errno));
    close(epoll_fd);
    close(server_socket);
    return 1;
  }

  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(PORT);
  server_address.sin_addr.s_addr = INADDR_ANY;

  if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1)
  {
    printf("Bind failed: %s\n", strerror(errno));
    close(epoll_fd);
    close(server_socket);
    return 1;
  }

  if (listen(server_socket, 1) == -1)
  {
    printf("Listen failed: %s\n", strerror(errno));
    close(epoll_fd);
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
      Client *client = events[i].data.ptr;
      int fd = client->fd;
      uint32_t flags = events[i].events;

      if (client->is_listener)
      {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len);
        if (client_socket == -1)
        {
          if (errno == EAGAIN || errno == EWOULDBLOCK)
          {
            continue;
            ;
          }
          else if (errno == EINTR)
          {
            continue;
          }
          printf("Accept failed: %s\n", strerror(errno));
          break;
        }

        int flags = fcntl(client_socket, F_GETFL, 0);                               // get current flags
        if (flags == -1 || fcntl(client_socket, F_SETFL, flags | O_NONBLOCK) == -1) // add nonblock flag
        {
          perror("fcntl");
          close(client_socket);
          continue;
        }
        printf("Client connected\n");

        Client *new_connection = calloc(1, sizeof(Client));
        if (new_connection == NULL)
        {
          close(client_socket);
          continue;
        }
        new_connection->fd = client_socket;

        struct epoll_event new_client;
        new_client.events = EPOLLIN;
        new_client.data.ptr = new_connection;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &new_client) != 0)
        {
          printf("epoll_ctl failed: %s \n", strerror(errno));
          free(new_connection);
          close(client_socket);
          continue;
        }
        continue;
      }
      if (flags & EPOLLERR)
      {
        perror("Socket error on fd");
        close_client(epoll_fd, client);
        continue;
      }
      if (flags & EPOLLIN)
      {
        char arguments[MAX_ARGUMENTS][MAX_ARGUMENT_LENGTH];
        size_t amount_arguments = 0;

        if (client->buffer_len == MAX_ARGUMENT_LENGTH)
        {
          close_client(epoll_fd, client);
          continue;
        }
        ssize_t bytes_received = recv(client->fd, client->buffer + client->buffer_len, MAX_ARGUMENT_LENGTH - client->buffer_len, 0);
        if (bytes_received > 0)
        {
          client->buffer_len += (size_t)bytes_received;

          client->buffer[client->buffer_len] = '\0';
          parseState state = parseRespString(client->buffer, client->buffer_len, arguments, &amount_arguments);
          switch (state)
          {
          case PARSE_FINISHED:
            int bytes_sent = echoCommand(client, amount_arguments, arguments);
            if (bytes_sent == -1)
            {
              if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
              {
                perror("send");
                close_client(epoll_fd, client);
                continue;
              }
            }
            client->buffer_len = 0;
            break;
          case PARSE_NEED_MORE_CHARS:

            break;
          case PARSE_INVALID:
            close_client(epoll_fd, client);
            break;
          default:
            break;
          }
        }
        else if (bytes_received == 0)
        {
          close_client(epoll_fd, client);
          continue;
        }
        else if (bytes_received == -1)
        {
          if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) // again, wouldblock, intr are errnos that indicate that you should try again later
          {
            continue;
          }
          perror("recv");
          close_client(epoll_fd, client);
          continue;
        }
      }
      else if (flags & EPOLLHUP)
      {
        close_client(epoll_fd, client);
      }
    }
  }

  close(server_socket);
  close(epoll_fd);

  return 0;
}