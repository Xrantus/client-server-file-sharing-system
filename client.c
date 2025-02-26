#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_COMMAND_LENGTH 100

// Handle file upload to server
void handle_upload(int sock, const char *filename)
{
    char buffer[BUFFER_SIZE];

    // Open file
    int file_fd = open(filename, O_RDONLY);
    if (file_fd < 0)
    {
        printf("Error: Cannot open file %s\n", filename);
        send(sock, "END_OF_UPLOAD", 13, 0);
        return;
    }

    // Wait for server ready signal
    memset(buffer, 0, sizeof(buffer));
    ssize_t recv_len = recv(sock, buffer, sizeof(buffer), 0);
    if (recv_len <= 0 || strstr(buffer, "READY_FOR_UPLOAD") == NULL)
    {
        printf("Server not ready for upload or error occurred\n");
        close(file_fd);
        return;
    }

    // Send file in chunks
    ssize_t bytes_read;
    size_t total_sent = 0;
    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0)
    {
        ssize_t bytes_sent = send(sock, buffer, bytes_read, 0);
        if (bytes_sent > 0)
        {
            total_sent += bytes_sent;
        }
        usleep(1000); // Small delay to prevent network congestion
    }

    close(file_fd);

    // Send end marker
    send(sock, "END_OF_UPLOAD", 13, 0);

    // Wait for server response
    memset(buffer, 0, sizeof(buffer));
    recv_len = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (recv_len > 0)
    {
        buffer[recv_len] = '\0';
        printf("%s", buffer);
    }
}

// Handle file download from server
void handle_download(int sock, const char *filename)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // Create/open local file
    int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0)
    {
        printf("Error: Cannot create file %s\n", filename);
        return;
    }

    // Receive file in chunks
    size_t total_received = 0;
    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0)
        {
            break;
        }

        // Check for error message
        if (strncmp(buffer, "ERROR:", 6) == 0)
        {
            printf("%s", buffer);
            close(file_fd);
            remove(filename);
            return;
        }

        // Check for end of file marker
        char *end_marker = strstr(buffer, "END_OF_FILE");
        if (end_marker != NULL)
        {
            // Write only up to the end marker
            ssize_t bytes_to_write = end_marker - buffer;
            if (bytes_to_write > 0)
            {
                write(file_fd, buffer, bytes_to_write);
                total_received += bytes_to_write;
            }
            break;
        }

        // Write to file
        write(file_fd, buffer, bytes_received);
        total_received += bytes_received;
    }

    close(file_fd);
    if (total_received > 0)
    {
        printf("File '%s' downloaded successfully (%zu bytes)\n", filename, total_received);
    }
    else
    {
        printf("Warning: File may be empty or transfer failed\n");
    }
}

int main(int argc, char *argv[])
{
    // Check command line arguments
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <username>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Initialize connection
    char username[50];
    strncpy(username, argv[1], sizeof(username) - 1);
    username[sizeof(username) - 1] = '\0';

    int client_socket;
    struct sockaddr_in server_addr;
    char command[MAX_COMMAND_LENGTH];
    char buffer[BUFFER_SIZE];

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0)
    {
        perror("Invalid address");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    // Send username
    snprintf(buffer, sizeof(buffer), "USERNAME %s", username);
    send(client_socket, buffer, strlen(buffer), 0);
    printf("Connecting as: %s\n", username);

    // Receive welcome message
    memset(buffer, 0, sizeof(buffer));
    if (recv(client_socket, buffer, sizeof(buffer), 0) > 0)
    {
        printf("%s", buffer);
    }

    printf("\nAvailable commands:\n");
    printf("LIST                  - List all files in server\n");
    printf("UPLOAD <filename>     - Upload a file to server\n");
    printf("DOWNLOAD <filename>   - Download a file from server\n");
    printf("DELETE <filename>     - Delete a file (admin only)\n");
    printf("RENAME <old> <new>    - Rename a file (admin only)\n");
    printf("EXIT                  - Disconnect from server\n\n");

    // Main command loop
    while (1)
    {
        printf("%s> ", username); // Changed prompt to show username
        fflush(stdout);

        if (!fgets(command, sizeof(command), stdin))
        {
            break;
        }

        command[strcspn(command, "\n")] = '\0';

        // Process user commands
        if (strcmp(command, "EXIT") == 0)
        {
            send(client_socket, command, strlen(command), 0);
            break;
        }
        else if (strncmp(command, "LIST", 4) == 0)
        {
            // Show file listing
            send(client_socket, command, strlen(command), 0);
            while (1)
            {
                memset(buffer, 0, sizeof(buffer));
                ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                if (bytes <= 0)
                    break;

                buffer[bytes] = '\0';
                printf("%s", buffer);
                if (strstr(buffer, "END_OF_LIST"))
                {
                    break;
                }
            }
        }
        else if (strncmp(command, "UPLOAD", 6) == 0)
        {
            // Handle file upload
            char *filename = command + 7;
            if (strlen(filename) == 0)
            {
                printf("Error: Please specify a filename\n");
                continue;
            }
            send(client_socket, command, strlen(command), 0);
            handle_upload(client_socket, filename);
        }
        else if (strncmp(command, "DOWNLOAD", 8) == 0)
        {
            // Handle file download
            char *filename = command + 9;
            if (strlen(filename) == 0)
            {
                printf("Error: Please specify a filename\n");
                continue;
            }
            send(client_socket, command, strlen(command), 0);
            handle_download(client_socket, filename);
        }
        else if (strncmp(command, "DELETE", 6) == 0 ||
                 strncmp(command, "RENAME", 6) == 0)
        {
            // Handle admin commands
            char *params = command + 7;
            if (strlen(params) == 0)
            {
                printf("Error: Please specify old and new filenames\n");
                continue;
            }
            send(client_socket, command, strlen(command), 0);
            memset(buffer, 0, sizeof(buffer));
            ssize_t recv_len = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
            if (recv_len > 0)
            {
                buffer[recv_len] = '\0';
                printf("%s", buffer);
            }
        }
        else
        {
            printf("Invalid command. Type 'EXIT' to quit.\n");
        }
    }

    // Cleanup and exit
    close(client_socket);
    printf("Disconnected from server.\n");
    return 0;
}