#define _DEFAULT_SOURCE
#include "rpc.h"
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <execinfo.h>

#define NONBLOCKING
#define MAX_BYTES 1001
#define MAX_DATA 100000
#define HEADER_LEN 4

struct rpc_server {
    int srv_socket;                 // server socket
    rpc_handle *handles_head;       // head of handlers linked list
    int num_handles;                // number of handlers
    int new_socket_fd;              // when connect with a client
};

struct rpc_client {
    int cli_socket;                 // client socket
    struct addrinfo *server_addr;   // server address
    char* addr;                     // client address
    int port;                       // port number
};

/* The node of the handler linked list */
struct rpc_handle {
    char name[MAX_BYTES];           // function name
    rpc_handler function;           // function
    rpc_handle* next;               // next handle
};

void* rpc_handle_client(void* serv);                            // client process
int send_data(int socket, rpc_data *payload, char *command);    // convert data to network byte order and send
int receive_data(int socket, rpc_data* result);                 // receive a data and convert to local byte order


/*
 * Creates and return a server structure with corresponding port,
 * create its address and socket, and bind to the socket.
 * */
rpc_server *rpc_init_server(int port) {
    int s, socket_fd;
    struct addrinfo hints, *res;
    char port_str[10];
    sprintf(port_str, "%d", port);

    /* Create address */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    s = getaddrinfo(NULL, port_str, &hints, &res);
    if (s != 0) {
        perror("getaddrinfo");
        exit(EXIT_FAILURE);
    }

    /* Create socket */
    socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (socket_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Reuse if possible */
    int enable = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    /* Bind the socket */
    if (bind(socket_fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    /* Create the server */
    rpc_server *server = malloc(sizeof(rpc_server));
    if (server == NULL) {
        exit(EXIT_FAILURE);
    }
    server->srv_socket = socket_fd;
    server->handles_head = NULL;
    server->num_handles = 0;
    server->new_socket_fd = 0;
    freeaddrinfo(res);
    return server;
}

/*
 * Server register a function.
 * Create handle for the handler and add to handle list of server.
 * Return the number of handles currently.
 * Return -1 with invalid input.
 * */
int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {

    /* Error handling */
    if (srv == NULL || name == NULL || handler == NULL) {
        return -1;
    }

    /* Create handle */
    rpc_handle *handle = (rpc_handle *) malloc(sizeof(rpc_handle));
    if (handle == NULL) {
        exit(EXIT_FAILURE);
    }

    /* Store variables in handle */
    size_t namelen = strlen(name) + 1;
    strncpy(handle->name, name, namelen);
    handle->function = handler;
    handle->next = NULL;

    /* Add the handle to the server's list of handles */
    if (srv->handles_head == NULL) {
        srv->handles_head = handle;
    } else {
        rpc_handle *current = srv->handles_head;
        while (current->next != NULL) {
            /* Replace if found repeated function */
            if (strcmp(current->next->name, handle->name) == 0) {
                rpc_handle *next = current->next->next;
                current->next = handle;
                handle->next = next;
                srv->num_handles += 1;
                return srv->num_handles;
            }
            current = current->next;
        }
        current->next = handle;
    }
    srv->num_handles += 1;
    return srv->num_handles;
}

/* Server handles a client.
 * Server receives a command, and send back signal or data with corresponding client call.
 * */
void* rpc_handle_client(void* serv) {
    rpc_server* srv = (rpc_server*) serv;
    int client_socket = srv->new_socket_fd;

    while (1) {
        char *buffer = malloc(MAX_BYTES);
        if (buffer == NULL) {
            exit(EXIT_FAILURE);
        }

        /* Read request from client */
        int num_bytes = read(client_socket, buffer, MAX_BYTES);
        if (num_bytes == -1) {
            free(buffer);
            return NULL;
        } else if (num_bytes == 0) {
            free(buffer);
            return NULL;
        }
        buffer[num_bytes] = '\0';

        /* Getting command */
        char *command, *saveptr;
        command = strtok_r(buffer, " ", &saveptr);

        /* If client called rpc_find */
        if (strcmp(command, "FIND") == 0) {
            char *name = strtok_r(NULL, " ", &saveptr);

            /* Finding handler */
            rpc_handle *handle = NULL;
            rpc_handle *curr = srv->handles_head;
            while (curr != NULL) {
                if (strcmp(curr->name, name) == 0) {
                    handle = curr;
                    break;
                }
                curr = curr->next;
            }
            if (handle == NULL) {
                char *null = "NULL";
                int bytes_sent = write(client_socket, null, strlen(null));
                if (bytes_sent < 0) {
                    continue;
                }
            }
            /* Send signal to the client */
            if (handle != NULL) {
                char *handle_str = "YESS";
                int bytes_sent = write(client_socket, handle_str, strlen(handle_str));
                if (bytes_sent < 0) {
                    continue;
                }
            }

        /* If client called rpc_call */
        } else if (strcmp(command, "CALL") == 0) {
            uint32_t func_len_nwb;
            num_bytes = read(client_socket, &func_len_nwb, sizeof(uint32_t));
            if (num_bytes == 0) {
                continue;
            }
            size_t func_len = (size_t) ntohl(func_len_nwb);

            char *func_name = malloc(func_len + 1);
            read(client_socket, func_name, func_len);
            func_name[func_len] = '\0';
            rpc_data data;
            receive_data(client_socket, &data);

            /* Finding handler */
            rpc_handle *handle = NULL;
            rpc_handle *curr = srv->handles_head;
            while (curr != NULL) {
                if (strcmp(curr->name, func_name) == 0) {
                    handle = curr;
                    break;
                }
                curr = curr->next;
            }

            /* Handle does not exist */
            if (handle == NULL) {
                char *null = "NULL";
                int bytes_sent = write(client_socket, null, strlen(null));
                if (bytes_sent < 0) {
                    continue;
                }
                continue;
            }
            rpc_data* result = handle->function(&data);
            if (result == NULL) {
                char *null = "NULL";
                int bytes_sent = write(client_socket, null, strlen(null));
                if (bytes_sent < 0) {
                    continue;
                }
            }
            if (result != NULL) {
                char *command_data = "DATA";
                if (send_data(client_socket, result, command_data) == 1) {
                    char *null = "NULL";
                    int bytes_sent = write(client_socket, null, strlen(null));
                    if (bytes_sent < 0) {
                        continue;
                    }
                }
            }
        }
        free(buffer);
    }
}

/* This function is responsible for accepting client connection and
 * creating a new thread for each client.
 * */
void rpc_serve_all(rpc_server *srv) {
    struct sockaddr_in6 client_addr;
    socklen_t client_addr_length;
    int socket_fd = srv->srv_socket;

    /* Server starts listening */
    if (listen(socket_fd, 5) < 0) {
        perror("server listen");
        exit(EXIT_FAILURE);
    }

    while (1) {
        client_addr_length = sizeof(client_addr);
        pthread_t thread_id;
        int new_socket_fd;

        /* Accept connections */
        new_socket_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &client_addr_length);
        if (new_socket_fd < 0) {
            continue;
        } else {
            srv->new_socket_fd = new_socket_fd;
        }
        if (pthread_create(&thread_id, NULL, rpc_handle_client, srv) < 0) {
            close(new_socket_fd);
            continue;
        }
        if (pthread_detach(thread_id) < 0) {
            close(new_socket_fd);
            continue;
        }
    }
}

/* Create and return a client with corresponding port number and address.
 * */
rpc_client *rpc_init_client(char *addr, int port) {
    int sockfd, s;
    struct addrinfo hints, *servinfo, *rp;
    char port_str[10];
    sprintf(port_str, "%d", port);

    /* Create address */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;

    s = getaddrinfo(addr, port_str, &hints, &servinfo);
    if (s != 0) {
        perror("getaddrinfo");
        exit(EXIT_FAILURE);
    }

    /* Connect to first valid result */
    for (rp = servinfo; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1)
            continue;
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;
        close(sockfd);
    }
    if (rp == NULL) {
        perror("failed to connect");
        exit(EXIT_FAILURE);
    }

    /* Create client */
    rpc_client *client =  (rpc_client *) malloc(sizeof(rpc_client));
    if (client == NULL) {
        exit(EXIT_FAILURE);
    }
    client->cli_socket = sockfd;
    client->addr = strdup(addr);
    client->port = port;
    client->server_addr = servinfo;
    return client;
}

/* Client find a server function with corresponding name.
 * Returns a handle if the function exists in server.
 * */
rpc_handle *rpc_find(rpc_client *cl, char *name) {
    if (cl == NULL || name == NULL) {
        return NULL;
    }

    /* Sending command and function name to server */
    char *command = calloc(MAX_BYTES, sizeof(char));
    if (command == NULL) {
        exit(EXIT_FAILURE);
    }
    char *find = "FIND ";
    strcat(command, find);
    strcat(command, name);
    int sockfd = cl->cli_socket;
    int len = strlen(command);
    write(sockfd, command, len);

    /* Receiving signal from server */
    char header[HEADER_LEN + 1] = {0};
    rpc_handle *handle = (rpc_handle *) malloc(sizeof(rpc_handle));
    if (handle == NULL) {
        exit(EXIT_FAILURE);
    }

    while (1) {
        int num_bytes = read(sockfd, header, HEADER_LEN);
        if (num_bytes > 0) {
            header[HEADER_LEN] = '\0';
            if (strcmp(header, "NULL") == 0) {
                free(command);
                return NULL;
            }
            size_t namelen = strlen(name) + 1;
            strncpy(handle->name, name, namelen);
            handle->function = NULL;
            handle->next = NULL;
            free(command);
            return handle;
        } else {
            return NULL;
        }
    }
}

/* Client call a server function with given data.
 * Returns a data if the procedure is called successfully.
 * */
rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    int sockfd = cl->cli_socket;
    /* Safety handling */
    if (cl == NULL || h == NULL || payload == NULL) {
        return NULL;
    }
    if ((payload->data2 == NULL && payload->data2_len != 0) || (payload->data2 != NULL && payload->data2_len == 0)) {
        return NULL;
    }
    if (payload->data2_len > MAX_DATA) {
        perror("Overlength error");
        exit(EXIT_FAILURE);
    }

    /* Sending call command */
    char* command = "CALL";
    write(sockfd, command, strlen(command) + 1);
    size_t func_len = strlen(h->name);
    uint32_t func_len_nwb = htonl((uint32_t) func_len);
    write(sockfd, &func_len_nwb, sizeof(uint32_t));
    write(sockfd, h->name, strlen(h->name));
    if (send_data(sockfd, payload, NULL) == 1) {
        return NULL;
    }

    /* Receiving result from server */
    while (1) {
        char* header = (char*) malloc( HEADER_LEN * sizeof(char));
        int num_bytes = read(sockfd, header, HEADER_LEN);
        header[4] = '\0';
        if (num_bytes > 0) {
            if (strcmp("NULL", header) == 0) {
                free(header);
                return NULL;
            }
            rpc_data* result = (rpc_data*) malloc(sizeof(rpc_data));
            receive_data(sockfd, result);
            return result;
        }
        free(header);
    }
}

/* This function closes the client socket and frees the client address.
 * */
void rpc_close_client(rpc_client *cl) {
    if (cl == NULL) {
        return;
    }

    /* Close client socket */
    if (cl->cli_socket != -1) {
        close(cl->cli_socket);
    }

    /* Free client address */
    if (cl->addr != NULL) {
        free(cl->addr);
    }

    if (cl->server_addr != NULL) {
        freeaddrinfo(cl->server_addr);
    }
    /* Free structure */
    free(cl);
}

/* Frees data2 and data */
void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}

/* Convert a data to network byte order and send the data
 * */
int send_data(int socket, rpc_data *payload, char *command) {

    /* Safety handling */
    if ((payload->data2 == NULL && payload->data2_len != 0) || (payload->data2 != NULL && payload->data2_len == 0)) {
        return 1;
    }

    if (command != NULL) {
        write(socket, command, strlen(command));
    }

    /* Send data 1 */
    int data1_copy = payload->data1;
    uint64_t data1_nwb = (uint64_t) data1_copy;
    data1_nwb = htobe64(data1_nwb);
    write(socket, &data1_nwb, sizeof(uint64_t));

    /* Send data2 len */
    size_t data2_len_copy = payload->data2_len;
    uint32_t data2_len_nwb = (uint32_t) data2_len_copy;
    data2_len_nwb = htonl(data2_len_nwb);
    write(socket, &data2_len_nwb, sizeof(uint32_t));

    /* Send data2 */
    if (data2_len_copy != 0) {
        write(socket, payload->data2, payload->data2_len);
    }
    return 0;
}

/* Receive a data in network byte order and convert to local host byte order.
 * */
int receive_data(int socket, rpc_data* result) {

    /* Receive data 1 */
    uint64_t data1_nwb;
    read(socket, &data1_nwb, sizeof(uint64_t));
    data1_nwb = be64toh(data1_nwb);
    int data1_rcv = (int) data1_nwb;

    /* Receive data 2 length */
    uint32_t data2_len_nwb;
    read(socket, &data2_len_nwb, sizeof(uint32_t));
    data2_len_nwb = ntohl(data2_len_nwb);
    size_t data2_len_rcv = (size_t) data2_len_nwb;

    /* Receive data 2 */
    if (data2_len_rcv != 0) {
        void* data2_rcv = malloc(data2_len_rcv);
        read(socket, data2_rcv, data2_len_rcv);
        result->data2 = data2_rcv;
    } else {
        result->data2 = NULL;
    }

    result->data1 = data1_rcv;
    result->data2_len = data2_len_rcv;
    return 0;
}