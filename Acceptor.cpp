#include "Acceptor.hpp"

#include <cstring>
#include <ev++.h>
#include <fstream>
#include <sys/socket.h>
#include <thread>

#include "Connection.hpp"

Acceptor::Acceptor(std::ofstream &log_file, struct ev_loop *loop, int sock, const char *rootDir)
    : w_accept(loop), log_file(log_file), rootDir(rootDir) {
    w_accept.set<Acceptor, &Acceptor::accept>(this);
    w_accept.start(sock, ev::READ);
}

void Acceptor::accept(ev::io &w, int revents) {
    int client_sd = ::accept4(w.fd, 0, 0, SOCK_NONBLOCK);
    if (-1 == client_sd) {
        char buffer[100];
        log_file << "Error accepting new connection: " << strerror_r(errno, buffer, 100) << std::endl;
        return;
    }

    std::thread t([](int client_sd, const char *rootDir) {
        std::ofstream log_file;
        log_file.open("/var/log/final.log", std::ofstream::out | std::ofstream::app);

        struct ev_loop *loop = ev_loop_new(0);
        if (NULL == loop) {
            log_file << "Can't create new events loop" << std::endl;
            return;
        }
        auto conn = new Connection(log_file, loop, client_sd, rootDir);
        if (NULL == conn) {
            log_file << "Not enough memory to create new connection" << std::endl;
            ev_loop_destroy(loop);
            return;
        }
        if (0 == ev_run(loop, 0)) {
            log_file << "Can't run events loop" << std::endl;
        }
        ev_loop_destroy(loop);
    }, client_sd, rootDir);
    t.detach();
};
