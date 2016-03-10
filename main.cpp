#include <arpa/inet.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ev++.h>
#include <getopt.h>
#include <fcntl.h>
#include <functional>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Acceptor.hpp"

#define PARAM_PARSING_ERROR() {\
                    perror("Can't parse arguments");\
                    exit(1);\
}

static inline void printUsage() {
    std::cerr << "Usage: ./final -h <ip> -p <port> -d <directory>"
              << std::endl;
}

int set_nonblock(int fd) {
    int flags;
#if defined(O_NONBLOCK)
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}

int main(int argc, char **argv) {
    char opt;
    char *ip;
    int port;
    char *directory;
    while (-1 != (opt = getopt(argc, argv, "h:p:d:"))) {
        switch (opt) {
            case 'h':
                ip = strdup(optarg);
                if (ip == NULL) PARAM_PARSING_ERROR();
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'd':
                directory = strdup(optarg);
                if (directory == NULL) PARAM_PARSING_ERROR();
                break;
            default:
                printUsage();
                exit(2);
        }
    }

    if (optind != 7) {
        printUsage();
        exit(2);
    }

    /*
    std::cout << "IP = '" << ip << "', Port = " << port << ", Directory = '"
                          << directory << '\'' << std::endl;
    */

    std::ofstream log_file;
    log_file.open("/var/log/final.log", std::ofstream::out | std::ofstream::app);
    if (!log_file) {
        std::cerr << "Can't open logfile for writing" << std::endl;
        exit(11);
    }

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

    pid_t pid;

    pid = fork();
    if (pid < 0) {
        log_file << "Can't fork: " << strerror(errno) << std::endl;
        exit(12);
    }

    // Terminate parent
    if (pid > 0) {
        exit(0);
    }

    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    struct ev_loop *loop = ev_default_loop(0);
    if (NULL == loop) {
        log_file << "could not initialise libev, bad $LIBEV_FLAGS in environment?"
                  << std::endl;
        exit(3);
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1) {
        log_file << "Can't create socket for listening: " << strerror(errno) << std::endl;
        exit(4);
    }

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   (const char*)&reuse, sizeof(reuse)) == -1) {
        log_file << "setsockopt(SO_REUSEADDR) failed: " << strerror(errno) << std::endl;
    }

// If we were NGINX
/*
#ifdef SO_REUSEPORT
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT,
                   (const char*)&reuse, sizeof(reuse)) == -1) {
        perror("setsockopt(SO_REUSEPORT) failed");
    }
#endif
*/

    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (0 == inet_aton(ip, &addr.sin_addr)) {
        log_file << "Invalid address: '" << ip << "'" << std::endl;
        exit(5);
    }

    if (-1 == set_nonblock(sock)) {
        log_file << "Can't set nonblocking mode for master: " <<
                    strerror(errno) << std::endl;
        exit(6);
    }

    if (-1 == bind(sock, (struct sockaddr *) &addr, sizeof(addr))) {
        log_file << "Can't bind to port: " << strerror(errno) << std::endl;
        exit(7);
    }

    if (-1 == listen(sock, SOMAXCONN)) {
        log_file << "Can't start listening port: " <<
                    strerror(errno) << std::endl;
        exit(8);
    }


    Acceptor acceptor(log_file, loop, sock, directory);

    if (0 == ev_run(loop, 0)) {
        log_file << "Can't start the events loop" << std::endl;
        exit(10);
    }

    return 0;
}
