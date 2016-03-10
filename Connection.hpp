#include <ev++.h>
#include <sstream>

class Connection {
public:
    Connection(std::ofstream &log_file, struct ev_loop *loop, int sock, const char *rootDir);
    ~Connection();
    void read(ev::io &w, int revents);
private:
    void badRequest(ev::io &w);
    void forbidden(ev::io &w);
    void internalError(ev::io &w);
    void notFound(ev::io &w);
    void handleLine(ev::io &w);
    void parse(char *buffer, ssize_t len, ev::io &w);
    void parseWord(ev::io &w);
    void respond(ev::io &w);
    static const size_t BUFFER_SIZE = 1024;

    ev::io w_client;
    std::ofstream &log_file;
    char *last_word;
    ssize_t lastWordLen;
    char *lastWord;
    bool isEmptyLine;
    int lineState;
    int wordState;
    int requestType;
    const char *rootDir;
    std::string filePath;
    bool isContentLength;
    int contentLength;
    bool closeConnection;
    bool notFinished;

    enum REQUEST_METHODS { GET, POST };
};
