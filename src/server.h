#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define MAX_CLIENTS 100

struct chat_server_t;

typedef struct client_t
{
    int socket;
    struct sockaddr_in address;
    char username[50];
    char ip_address[INET_ADDRSTRLEN];
    struct chat_server_t *server;
} client_t;

typedef struct chat_server_t
{
    int id;
    int port;
    int server_socket;
    client_t *clients[MAX_CLIENTS];
    int client_count;
    pthread_mutex_t clients_mutex;
    pthread_t thread;
    bool running;
} chat_server_t;

// Function declarations
void *run_server(void *arg);
void broadcast_message(struct chat_server_t *server, char *message, int sender_socket);
chat_server_t *create_chat_server(int id, int port);
void stop_chat_server(chat_server_t *server);
void remove_client(chat_server_t *server, client_t *client);

#endif
