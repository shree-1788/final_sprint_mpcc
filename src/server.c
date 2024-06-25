#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <stdbool.h>
#include "server.h"
#include "user.h"
#include "encryption.h"
#include "logger.h"

#define MAX_MESSAGE_LENGTH 1024

void broadcast_message(chat_server_t *server, char *message, int sender_socket)
{
    pthread_mutex_lock(&server->clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (server->clients[i] && server->clients[i]->socket != sender_socket)
        {
            char encrypted_msg[MAX_MESSAGE_LENGTH];
            custom_encrypt(message, encrypted_msg);
            send(server->clients[i]->socket, encrypted_msg, strlen(encrypted_msg), 0);
        }
    }
    pthread_mutex_unlock(&server->clients_mutex);
}

void remove_client(chat_server_t *server, client_t *client)
{
    pthread_mutex_lock(&server->clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (server->clients[i] == client)
        {
            server->clients[i] = NULL;
            server->client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&server->clients_mutex);

    close(client->socket);
    free(client);
}

void *handle_client(void *arg)
{
    client_t *client = (client_t *)arg;
    chat_server_t *server = (chat_server_t *)client->server;
    char buffer[MAX_MESSAGE_LENGTH];
    int read_size;

    // Wait for the username
    read_size = recv(client->socket, buffer, sizeof(buffer) - 1, 0);
    if (read_size > 0)
    {
        buffer[read_size] = '\0';
        if (strncmp(buffer, "USERNAME:", 9) == 0)
        {
            strncpy(client->username, buffer + 9, sizeof(client->username) - 1);
            client->username[sizeof(client->username) - 1] = '\0';
            log_info("User %s connected from IP %s", client->username, client->ip_address);
        }
    }

    while ((read_size = recv(client->socket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[read_size] = '\0';
        char decrypted_msg[MAX_MESSAGE_LENGTH];
        custom_decrypt(buffer, decrypted_msg);

        char broadcast_buffer[MAX_MESSAGE_LENGTH + 100];
        snprintf(broadcast_buffer, sizeof(broadcast_buffer), "%s (%s): %s", client->username, client->ip_address, decrypted_msg);
        broadcast_message(server, broadcast_buffer, client->socket);
    }

    if (read_size == 0)
    {
        log_info("Client %s disconnected", client->username);
    }
    else if (read_size == -1)
    {
        log_error("Receive failed from client %s", client->username);
    }

    remove_client(server, client);

    if (server->client_count == 0)
    {
        log_info("No more clients on server %d. Stopping server.", server->id);
        server->running = false;
	pthread_exit(NULL);
    }

    return NULL;
}

void *run_server(void *arg)
{
    chat_server_t *server = (chat_server_t *)arg;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);

    server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_socket == -1)
    {
        log_fatal("Error creating server socket for server %d", server->id);
        return NULL;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server->port);

    if (bind(server->server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        log_fatal("Error binding server socket for server %d", server->id);
        return NULL;
    }

    if (listen(server->server_socket, 10) < 0)
    {
        log_fatal("Error listening on server socket for server %d", server->id);
        return NULL;
    }

    log_info("Server %d started on port %d. Waiting for connections...", server->id, server->port);

    while (server->running)
    {
        int client_socket = accept(server->server_socket, (struct sockaddr *)&client_addr, &addr_size);
        if (client_socket < 0)
        {
            if (server->running)
            {
                log_error("Error accepting client connection on server %d", server->id);
            }
            continue;
        }

        client_t *client = malloc(sizeof(client_t));
        if (client == NULL)
        {
            log_error("Failed to allocate memory for client on server %d", server->id);
            close(client_socket);
            continue;
        }

        client->socket = client_socket;
        client->address = client_addr;
        client->server = server;
        inet_ntop(AF_INET, &(client->address.sin_addr), client->ip_address, INET_ADDRSTRLEN);

        pthread_mutex_lock(&server->clients_mutex);
        int i;
        for (i = 0; i < MAX_CLIENTS; i++)
        {
            if (server->clients[i] == NULL)
            {
                server->clients[i] = client;
                server->client_count++;
                break;
            }
        }
        pthread_mutex_unlock(&server->clients_mutex);

        if (i == MAX_CLIENTS)
        {
            log_error("Server %d is full, connection rejected", server->id);
            close(client_socket);
            free(client);
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void *)client) != 0)
        {
            log_error("Error creating thread for client on server %d", server->id);
            remove_client(server, client);
        }
        else
        {
            pthread_detach(thread_id);
        }
    }

    log_info("Server %d on port %d is shutting down", server->id, server->port);
    close(server->server_socket);

    return NULL;
}

chat_server_t *create_chat_server(int id, int port)
{
    log_debug("Creating chat server with id %d and port %d", id, port);
    if (id <= 0 || port <= 0 || port > 65535) {
        log_error("Invalid id or port number");
        return NULL;
    }
    chat_server_t *server = malloc(sizeof(chat_server_t));
    if (server == NULL)
    {
        log_error("Failed to allocate memory for chat server");
        return NULL;
    }

    server->id = id;
    server->port = port;
    server->client_count = 0;
    server->running = true;

    if (pthread_mutex_init(&server->clients_mutex, NULL) != 0) {
        log_error("Failed to initialize clients mutex");
        free(server);
        return NULL;
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        server->clients[i] = NULL;
    }

    if (pthread_create(&server->thread, NULL, run_server, (void *)server) != 0)
    {
        log_error("Failed to create thread for chat server %d", id);
        pthread_mutex_destroy(&server->clients_mutex);
        free(server);
        return NULL;
    }

    log_debug("Chat server thread created successfully.");
    return server;
}

void stop_chat_server(chat_server_t *server)
{
    if (!server->running)
    {
        return;
    }

    server->running = false;
    shutdown(server->server_socket, SHUT_RDWR);
    close(server->server_socket);

    pthread_join(server->thread, NULL);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (server->clients[i])
        {
            close(server->clients[i]->socket);
            free(server->clients[i]);
        }
    }

    pthread_mutex_destroy(&server->clients_mutex);
    log_info("Server %d on port %d has been stopped", server->id, server->port);
}
