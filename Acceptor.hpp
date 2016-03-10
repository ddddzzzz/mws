#include <sstream>
#include <ev++.h>

class Acceptor {
public:
    Acceptor(std::ofstream &log_file, struct ev_loop *ev_loop, int sock,
             const char *rootDir);
    void accept(ev::io &w, int revents);

private:
    ev::io w_accept;
    std::ofstream &log_file;
    const char *rootDir;
};
