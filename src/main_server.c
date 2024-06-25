#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include "logger.h"
#include "server.h"

#define MAX_SERVERS 100
#define MAIN_SERVER_PORT 7000
#define MAX_COMMAND_LENGTH 1024

chat_server_t *servers[MAX_SERVERS];
int server_count = 0;
pthread_mutex_t servers_mutex = PTHREAD_MUTEX_INITIALIZER;

void remove_server(int index)
{
    pthread_mutex_lock(&servers_mutex);
    if (index < server_count - 1)
    {
        memmove(&servers[index], &servers[index + 1], (server_count - index - 1) * sizeof(chat_server_t *));
    }
    server_count--;
    pthread_mutex_unlock(&servers_mutex);
}

void *monitor_server(void *arg)
{
    chat_server_t *server = (chat_server_t *)arg;
    pthread_join(server->thread,NULL);
    while (server->running)
    {
        sleep(1);
    }
    log_info("Server %d on port %d has stopped", server->id, server->port);

    pthread_mutex_lock(&servers_mutex);
    for (int i = 0; i < server_count; i++)
    {
        if (servers[i] == server)
        {
            remove_server(i);
            break;
        }
    }
    pthread_mutex_unlock(&servers_mutex);
stop_chat_server(server);
    free(server);
    return NULL;
}

void handle_admin_commands()
{
    char command[MAX_COMMAND_LENGTH];
    while (1)
    {
        printf("Enter command (create <port> | list | exit): ");
        if (fgets(command, sizeof(command), stdin) == NULL)
        {
            log_error("Error reading command");
            break;
        }

        size_t len = strlen(command);
        if (len > 0 && command[len-1] == '\n') {
            command[len-1] = '\0';
            len--;
        }

        log_debug("Received command: %s", command);

        if (strncmp(command, "create ", 7) == 0)
        {
            char *port_str = command + 7;
            log_debug("Port string: '%s'", port_str);

            if (*port_str == '\0' || strspn(port_str, "0123456789") != strlen(port_str)) {
                log_error("Invalid port number format");
                printf("Invalid port number format. Please use only digits.\n");
                continue;
            }

            int port = atoi(port_str);
            log_debug("Parsed port number: %d", port);
            if (port > 0 && port <= 65535)
            {
                log_info("Attempting to create server on port %d", port);
                chat_server_t *server = create_chat_server(server_count + 1, port);
                if(server != NULL) {
                    pthread_mutex_lock(&servers_mutex);
                    if (server_count < MAX_SERVERS)
                    {
                        servers[server_count++] = server;
                        log_info("Created chat server on port %d", server->port);
                        
                        pthread_t monitor_thread;
                        if (pthread_create(&monitor_thread, NULL, monitor_server, server) != 0)
                        {
                            log_error("Failed to create monitor thread for server on port %d", port);
                        }
                        else
                        {
                            pthread_detach(monitor_thread);
                        }
                    }
                    else
                    {
                        log_error("Maximum number of servers reached");
                        stop_chat_server(server);
                        free(server);
                    }
                    pthread_mutex_unlock(&servers_mutex);
                }
                else
                {
                    log_error("Failed to create chat server on port %d", port);
                }
            }
            else
            {
                log_error("Invalid port number: %d", port);
                printf("Invalid port number. Please use a number between 1 and 65535.\n");
            }
        }
        else if (strcmp(command, "list") == 0)
        {
            printf("Active servers:\n");
            pthread_mutex_lock(&servers_mutex);
            for (int i = 0; i < server_count; i++)
            {
                if (servers[i] != NULL && servers[i]->running)
                {
                    printf("Server ID: %d, Port: %d\n", servers[i]->id, servers[i]->port);
                }
            }
            if (server_count == 0)
            {
                printf("No active servers.\n");
            }
            pthread_mutex_unlock(&servers_mutex);
        }
        else if (strcmp(command, "exit") == 0)
        {
            log_info("Exiting main server");
            pthread_mutex_lock(&servers_mutex);
            for (int i = 0; i < server_count; i++)
            {
                if (servers[i] != NULL)
                {
                    stop_chat_server(servers[i]);
                }
            }
            pthread_mutex_unlock(&servers_mutex);
            break;
        }
        else
        {
            log_warning("Unknown command: %s", command);
            printf("Unknown command\n");
        }
    }
}

int main()
{
    if (init_logger() != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }
    log_info("Main server started. Logging initialized.");

    log_info("Entering command loop");
    handle_admin_commands();

    log_info("Main server shutting down");
    
    // Wait for all servers to fully stop and be removed
    while (1)
    {
        pthread_mutex_lock(&servers_mutex);
        if (server_count == 0)
        {
            pthread_mutex_unlock(&servers_mutex);
            break;
        }
        pthread_mutex_unlock(&servers_mutex);
        sleep(1);
    }

    pthread_mutex_destroy(&servers_mutex);
    return 0;
}
