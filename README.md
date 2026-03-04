# TCP Multi-Client Chat Server (C)

A terminal-based multi-client chat application implemented in C using POSIX sockets and pthreads.

## Features

- Multi-client support using pthread threads
- TCP socket communication
- Broadcast messaging between connected clients
- Username-based chat
- Thread-safe client list using mutex

## Technologies

- C
- POSIX Sockets
- pthreads
- Linux

## Demo

Server:
./server 8080

Client:
./client 127.0.0.1 8080

## Build

Compile using Make:

```bash
make
