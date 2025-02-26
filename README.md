# Client-Server File Sharing System

This project implements a client-server file sharing system in C, utilizing TCP sockets for reliable data transfer.

## Features

- **List Files**: List all files on the server.
- **Upload Files**: Upload files from the client to the server.
- **Download Files**: Download files from the server to the client.
- **Delete Files**: Delete files on the server (admin only).
- **Rename Files**: Rename files on the server (admin only).

## Getting Started

### Prerequisites

- GCC compiler

### Building the Project

To build the project, run the following command:

```sh
make
```

### Running the Server

To start the server, run:

```sh
./server
```

### Running the Client

To start the client, run:

```sh
./client <username>
```

## Usage

Once connected, the client can use the following commands:

- `LIST`: List all files on the server.
- `UPLOAD <filename>`: Upload a file to the server.
- `DOWNLOAD <filename>`: Download a file from the server.
- `DELETE <filename>`: Delete a file on the server (admin only).
- `RENAME <old> <new>`: Rename a file on the server (admin only).
- `EXIT`: Disconnect from the server.

## Cleaning Up

To clean up the build files, run:

```sh
make clean
```

## License

This project is licensed under the MIT License.