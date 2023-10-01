#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "friends.h"
#include "server.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

Client *clients = NULL;
User *user_list = NULL;

Client *find_client(const int fd, const Client *head)
{
    while (head != NULL && fd != head->sock_fd)
    {
        head = head->next;
    }

    return (Client *)head;
}

int create_client(const int fd)
{
    Client *new_client = malloc(sizeof(Client));
    ClientState *new_state = malloc(sizeof(ClientState));
    if (new_client == NULL || new_state == NULL)
    {
        perror("malloc");
        exit(1);
    }

    new_state->buffer = malloc(BUFSIZ);
    if (new_state->buffer == NULL)
    {
        perror("malloc");
        exit(1);
    }
    memset(new_state->buffer, '\0', BUFSIZ);

    new_state->after = new_state->buffer;
    new_state->room = BUFSIZ;
    new_state->inbuf = 0;

    new_client->sock_fd = fd;
    new_client->username = NULL;
    new_client->next = NULL;
    new_client->state = new_state;

    // Add client to list
    Client *prev = NULL;
    Client *curr = clients;

    while (curr != NULL && curr->sock_fd != fd)
    {
        prev = curr;
        curr = curr->next;
    }

    if (clients == NULL)
    {
        clients = new_client;
        return 0;
    }
    else if (curr != NULL)
    {
        free(new_client);
        return 1;
    }
    else
    {
        prev->next = new_client;
        return 0;
    }
}

/*
 * Accept a connection. Note that a new file descriptor is created for
 * communication with the client. The initial socket descriptor is used
 * to accept connections, but the new socket is used to communicate.
 * Return the new client's file descriptor or -1 on error.
 */
int accept_connection(int fd)
{
    int client_fd = accept(fd, NULL, NULL);

    if (client_fd < 0)
    {
        perror("server: accept");
        close(fd);
        return -1;
    }

    // Client *client_list = *client_list_ptr;
    // create_client(client_fd, &client_list);

    switch (create_client(client_fd))
    {
    case 1:
        printf("client by this name already exists");
        break;
    case 0:
        printf("client created successfully");
        break;
    }

    // Send Welcome message
    write(client_fd, WELCOME_MESSAGE, sizeof(WELCOME_MESSAGE));

    return client_fd;
}

void process_messages(int sock_fd)
{
    // Client *client_list = *client_list_ptr;

    // The client accept - message accept loop. First, we prepare to listen to multiple
    // file descriptors by initializing a set of file descriptors.
    int max_fd = sock_fd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(sock_fd, &all_fds);

    while (1)
    {
        // select updates the fd_set it receives, so we always use a copy and retain the original.
        fd_set listen_fds = all_fds;
        if (select(max_fd + 1, &listen_fds, NULL, NULL, NULL) == -1)
        {
            perror("server: select");
            exit(1);
        }

        // Is it the original socket? Create a new connection ...
        if (FD_ISSET(sock_fd, &listen_fds))
        {
            int client_fd = accept_connection(sock_fd);
            if (client_fd > max_fd)
            {
                max_fd = client_fd;
            }
            FD_SET(client_fd, &all_fds);
            printf("Accepted connection\n"); // TODO: Remove
        }

        // Next, check the clients.
        Client *head = clients;

        while (head != NULL)
        {
            if (head->sock_fd > -1 && FD_ISSET(head->sock_fd, &listen_fds))
            {
                int client_closed = read_from(head);
                if (client_closed > 0)
                {
                    FD_CLR(client_closed, &all_fds);
                    printf("Client %d disconnected\n", client_closed); // TODO: Remove
                }
                else
                {
                    //printf("Echoing message from client %d\n", head->sock_fd); //TODO: Remove
                    //  Process Commands
                }
            }

            head = head->next;
        }
    }
}

int read_from(Client *client)
{
    int nbytes = read(client->sock_fd, client->state->after, client->state->room);

    if (nbytes <= 0)
    {
        return client->sock_fd;
    }

    client->state->inbuf += nbytes;

    int where = find_network_newline(client->state->buffer, client->state->inbuf);
    if (where > 0)
    {
        client->state->buffer[where - 2] = '\0';
        // printf("Next message: %s\n", client->state->buffer);

        char *commands[10];
        int cmdCnt = tokenize(client->state->buffer, commands);

        int result = process_commands(client, commands, cmdCnt);
        if(result < 0){ //User has quit program
            return client->sock_fd;
        }

        memmove(client->state->buffer, client->state->buffer + where, BUFSIZ - where);

        client->state->inbuf -= where;
        client->state->room += (nbytes - where);
        client->state->after = client->state->buffer + client->state->inbuf;
    }
    else
    {
        client->state->room -= nbytes;
        client->state->after += nbytes;
    }

    return 0;
}

int process_commands(Client *client, char **commands, int cmdCnt)
{
    // Add new username for user
    if (client->username == NULL)
    {
        char username[32];
        strncpy(username, commands[0], 32);
        User* user = find_user(username, user_list);
        
        if(user == NULL){
            create_user(username, &user_list);
        }
        
        client->username = malloc(strlen(username));
        strcpy(client->username, username);

        send(client->sock_fd, PROMPT_USER,sizeof(PROMPT_USER),0);
    }
    else if (strcmp(commands[0], "list_users") == 0 && cmdCnt == 1)
    {
        char* buf = list_users(user_list);
        send(client ->sock_fd, buf, strlen(buf),0);
        free(buf);
    }
    else if (strcmp(commands[0], "make_friends") == 0 && cmdCnt == 2)
    {
        switch (make_friends(client->username, commands[1], user_list)) {
            case 1:
                perror("users are already friends");
                break;
            case 2:
                perror("at least one user you entered has the max number of friends");
                break;
            case 3:
                perror("you must enter two different users");
                break;
            case 4:
                perror("at least one user you entered does not exist");
                break;
        }
    }
    else if (strcmp(commands[0], "post") == 0 && cmdCnt >= 3)
    {
        // first determine how long a string we need
        int space_needed = 0;
        for (int i = 2; i < cmdCnt; i++) {
            space_needed += strlen(commands[i]) + 1;
        }

        // allocate the space
        char *contents = malloc(space_needed);
        if (contents == NULL) {
            perror("malloc");
            exit(1);
        }

        // copy in the bits to make a single string
        strcpy(contents, commands[2]);
        for (int i = 3; i < cmdCnt; i++) {
            strcat(contents, " ");
            strcat(contents, commands[i]);
        }

        User *author = find_user(client->username, user_list);
        User *target = find_user(commands[1], user_list);
        switch (make_post(author, target, contents)) {
            case 1:
                perror("the users are not friends");
                break;
            case 2:
                perror("at least one user you entered does not exist");
                break;
        }
    }
    else if (strcmp(commands[0], "profile") == 0 && cmdCnt == 2)
    {
        User *user = find_user(commands[1], user_list);
        char * buf = print_user(user);

        if (buf == NULL)
        {
            perror("user not found");
        }else{
            send(client ->sock_fd, buf, strlen(buf),0);
        }

        free(buf);
    }
    else if (strcmp(commands[0], "quit") == 0 && cmdCnt == 1)
    {
        return -1;
    }else{
        perror("Incorrect syntax");
    }
    
    send(client->sock_fd, "> ",3,0);

    return 0;
}

/*
 * Search the first n characters of buf for a network newline (\r\n).
 * Return one plus the index of the '\n' of the first network newline,
 * or -1 if no network newline is found.
 * Definitely do not use strchr or other string functions to search here. (Why not?)
 */
int find_network_newline(const char *buf, int n)
{
    int i = 0;
    while (i < (n - 1))
    {
        if (buf[i] == '\r' && buf[i + 1] == '\n')
        {
            return i + 2;
        }
        i++;
    }

    return -1;
}

int tokenize(char *cmd, char **commands)
{
    int cnt = 0;
    char *next_token = strtok(cmd, " ");

    while (next_token != NULL)
    {
        commands[cnt] = next_token;
        cnt++;
        next_token = strtok(NULL, " ");
    }

    return cnt;
}