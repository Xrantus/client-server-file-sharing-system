#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define FILE_DIRECTORY "./server_files"
#define MAX_CLIENTS 10

// Client structure to store connection information
typedef struct
{
    int socket;
    int uid;
    int is_admin;
    char username[50];
} Client;

// Global client array and counter
Client clients[MAX_CLIENTS];
int client_count = 0;

// Notify clients about admin changes
void broadcast_admin_change(int new_admin_uid)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].socket != -1)
        {
            if (clients[i].uid == new_admin_uid)
            {
                send(clients[i].socket, "You are now the admin\n", 22, 0);
                clients[i].is_admin = 1;
            }
            else
            {
                clients[i].is_admin = 0;
            }
        }
    }
}

// Remove client and reassign admin if needed
void remove_client(int uid)
{
    char username[50] = "";
    int was_admin = 0;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].uid == uid)
        {
            strncpy(username, clients[i].username, sizeof(username) - 1);
            was_admin = clients[i].is_admin;
            printf("[INFO] Client disconnected - UID: %d, Username: %s, Was Admin: %d\n",
                   uid, username, was_admin);

            close(clients[i].socket);
            clients[i].socket = -1;
            clients[i].uid = -1;
            clients[i].is_admin = 0;
            memset(clients[i].username, 0, sizeof(clients[i].username));
            client_count--;
            break;
        }
    }

    if (was_admin && client_count > 0)
    {
        int new_admin_assigned = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].socket != -1)
            {
                broadcast_admin_change(clients[i].uid);
                printf("[INFO] Admin rights transferred to UID: %d, Username: %s\n",
                       clients[i].uid, clients[i].username);
                new_admin_assigned = 1;
                break;
            }
        }

        if (!new_admin_assigned)
        {
            printf("[INFO] No active clients to assign as admin\n");
        }
    }
}

// List all files in server directory
void list_files(int client_socket)
{
    DIR *dir = opendir(FILE_DIRECTORY);
    if (!dir)
    {
        send(client_socket, "ERROR: Cannot list files\n", 26, 0);
        return;
    }

    send(client_socket, "\nFile Listing:\n\n", 15, 0);
    send(client_socket, "----------------------------------------\n", 41, 0);

    struct dirent *entry;
    struct stat file_stat;
    char filepath[BUFFER_SIZE];
    char file_info[BUFFER_SIZE];
    int files_found = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(filepath, sizeof(filepath), "%s/%s", FILE_DIRECTORY, entry->d_name);
        if (stat(filepath, &file_stat) == 0)
        {
            if (S_ISREG(file_stat.st_mode))
            {
                snprintf(file_info, sizeof(file_info), "%-30s %ld bytes\n",
                         entry->d_name, (long)file_stat.st_size);
                send(client_socket, file_info, strlen(file_info), 0);
                files_found++;
            }
        }
        else
        {
            fprintf(stderr, "Error stating file %s: %s\n", filepath, strerror(errno));
        }
    }

    if (files_found == 0)
    {
        send(client_socket, "No files found\n", 14, 0);
    }

    send(client_socket, "----------------------------------------\n", 41, 0);
    send(client_socket, "END_OF_LIST\n", 12, 0);

    closedir(dir);
}

// Handle file upload from client
void handle_upload(int client_socket, const char *filename)
{
    char filepath[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    snprintf(filepath, sizeof(filepath), "%s/%s", FILE_DIRECTORY, filename);

    int file_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0)
    {
        send(client_socket, "ERROR: Cannot create file\n", 25, 0);
        return;
    }

    send(client_socket, "READY_FOR_UPLOAD\n", 17, 0);

    while (1)
    {
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0)
        {
            break;
        }
        if (strstr(buffer, "END_OF_UPLOAD") != NULL)
            break;
        write(file_fd, buffer, bytes_read);
    }

    close(file_fd);
    send(client_socket, "File uploaded successfully\n", 28, 0);
    printf("[INFO] File upload completed: %s\n", filename);
}

// Handle file download to client
void handle_download(int client_socket, const char *filename)
{
    char filepath[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    snprintf(filepath, sizeof(filepath), "%s/%s", FILE_DIRECTORY, filename);
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0)
    {
        send(client_socket, "ERROR: File not found\n", 21, 0);
        return;
    }

    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0)
    {
        send(client_socket, buffer, bytes_read, 0);
    }

    close(file_fd);
    send(client_socket, "END_OF_FILE\n", 12, 0);
    printf("[INFO] File download completed: %s\n", filename);
}

// Main client handler - processes all client commands
void handle_client(int client_socket, int uid)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    int is_admin = (client_count == 1);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].socket == -1)
        {
            clients[i].socket = client_socket;
            clients[i].uid = uid;
            clients[i].is_admin = is_admin;
            memset(clients[i].username, 0, sizeof(clients[i].username));
            break;
        }
    }

    if ((bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_read] = '\0';
        if (strncmp(buffer, "USERNAME ", 9) == 0)
        {
            char *username_str = buffer + 9;
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i].uid == uid)
                {
                    strncpy(clients[i].username, username_str,
                            sizeof(clients[i].username) - 1);
                    printf("[INFO] User connected - UID: %d, Username: %s\n",
                           uid, clients[i].username);
                    break;
                }
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].uid == uid)
        {
            char welcome_msg[BUFFER_SIZE];
            if (clients[i].is_admin)
            {
                snprintf(welcome_msg, sizeof(welcome_msg),
                         "Welcome %s! You are the admin.\n", clients[i].username);
            }
            else
            {
                snprintf(welcome_msg, sizeof(welcome_msg),
                         "Welcome %s! You are a regular user.\n", clients[i].username);
            }
            send(client_socket, welcome_msg, strlen(welcome_msg), 0);
            break;
        }
    }

    // Process client commands
    while ((bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_read] = '\0';

        // Check if client is admin
        int current_is_admin = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].uid == uid)
            {
                current_is_admin = clients[i].is_admin;
                break;
            }
        }

        // Handle different commands
        if (strncmp(buffer, "LIST", 4) == 0)
        {
            list_files(client_socket);
        }
        // File operations
        else if (strncmp(buffer, "UPLOAD", 6) == 0)
        {
            handle_upload(client_socket, buffer + 7);
        }
        else if (strncmp(buffer, "DOWNLOAD", 8) == 0)
        {
            handle_download(client_socket, buffer + 9);
        }
        // Admin operations
        else if (current_is_admin && strncmp(buffer, "DELETE", 6) == 0)
        {
            char filepath[BUFFER_SIZE];
            char *filename = buffer + 7;
            snprintf(filepath, sizeof(filepath), "%s/%s", FILE_DIRECTORY, filename);
            if (remove(filepath) == 0)
            {
                send(client_socket, "File deleted successfully\n", 27, 0);
                printf("[INFO] File deleted by admin %s: %s\n",
                       clients[uid].username, filename);
            }
            else
            {
                send(client_socket, "ERROR: Cannot delete file\n", 28, 0);
            }
        }
        else if (current_is_admin && strncmp(buffer, "RENAME", 6) == 0)
        {
            char *old_name = strtok(buffer + 7, " ");
            char *new_name = strtok(NULL, " \n");
            if (old_name && new_name)
            {
                char old_path[BUFFER_SIZE], new_path[BUFFER_SIZE];
                snprintf(old_path, sizeof(old_path), "%s/%s",
                         FILE_DIRECTORY, old_name);
                snprintf(new_path, sizeof(new_path), "%s/%s",
                         FILE_DIRECTORY, new_name);
                if (rename(old_path, new_path) == 0)
                {
                    send(client_socket, "File renamed successfully\n\n", 28, 0);
                    printf("[INFO] File renamed by admin %s: %s -> %s\n",
                           clients[uid].username, old_name, new_name);
                }
                else
                {
                    send(client_socket, "ERROR: Cannot rename file\n", 28, 0);
                }
            }
            else
            {
                send(client_socket, "ERROR: Invalid rename format\n", 30, 0);
            }
        }
        else if (strcmp(buffer, "EXIT") == 0)
        {
            break;
        }
        else
        {
            send(client_socket, "ERROR: Invalid command\n", 24, 0);
        }
    }

    remove_client(uid);
}

int main()
{
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int uid = 0;

    // Clear client array on startup
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].socket = -1;
        clients[i].uid = -1;
        clients[i].is_admin = 0;
        memset(clients[i].username, 0, sizeof(clients[i].username));
    }

    mkdir(FILE_DIRECTORY, 0755);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d...\n", PORT);

    // Main server loop - accept new connections
    while (1)
    {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0)
        {
            perror("Accept failed");
            continue;
        }

        if (client_count >= MAX_CLIENTS)
        {
            send(client_socket, "Server is full\n", 15, 0);
            close(client_socket);
            continue;
        }

        client_count++;
        printf("New client connected. UID: %d\n", uid);

        pid_t pid = fork();
        if (pid == 0)
        {
            close(server_socket);
            handle_client(client_socket, uid);
            exit(0);
        }
        else if (pid < 0)
        {
            perror("Fork failed");
            close(client_socket);
            client_count--;
            continue;
        }
        close(client_socket);
        uid++;
    }

    close(server_socket);
    return 0;
}
