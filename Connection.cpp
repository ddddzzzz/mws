#include "Connection.hpp"

#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>

Connection::Connection(std::ofstream &log_file, struct ev_loop *loop, int sock,
                       const char *rootDir) :
    w_client(loop), log_file(log_file), lastWordLen(0), isEmptyLine(true),
    lineState(0), wordState(0), requestType(-1), rootDir(rootDir),
    filePath(), isContentLength(false), contentLength(-1), closeConnection(false),
    notFinished(true) {
    lastWord = new char[BUFFER_SIZE];
    w_client.set<Connection, &Connection::read>(this);
    w_client.start(sock, ev::READ);
}

Connection::~Connection() {
    delete[] lastWord;
}

void Connection::handleLine(ev::io &w) {
    if (isEmptyLine) {
        return respond(w);
    }
    if (notFinished) {
        badRequest(w);
        return;
    }
    isEmptyLine = true;
    lineState++;
    wordState = 0;
}

void Connection::parseWord(ev::io &w) {
    isEmptyLine = false;
    if (lineState == 0) {
        switch (wordState) {
            case 0:
                {
                    wordState++;
                    notFinished = true;
                    if (lastWordLen == 3 && strncmp(lastWord, "GET", 3) == 0) {
                        requestType = GET;
                    } else {
                        log_file << "Wrong method: " << lastWord << std::endl;
                        badRequest(w);
                        return;
                    }
                    break;
                }
            case 1:
                {
                    wordState++;
                    if (lastWord[0] != '/') {
                        badRequest(w);
                        return;
                    }
                    if (strstr(lastWord, "/..")) {
                        log_file << "Wrong path: " << lastWord << std::endl;
                        badRequest(w);
                        return;
                    } else {
                        filePath = lastWord;
                        filePath = filePath.substr(0, filePath.find("?"));
                    }
                    break;
                }
            case 2:
                {
                    wordState++;
                    notFinished = false;
                    if (lastWordLen != 8 || 0 != strncmp(lastWord, "HTTP/1.0", lastWordLen)) {
                        log_file << "Wrong protocol: " << lastWord << std::endl;
                        badRequest(w);
                        return;
                    }
                    break;
                }
            default:
                {
                    log_file << "Out of line 1 " << lastWord << std::endl;
                    badRequest(w);
                    return;
                }
        }
    } else {
        char *colonPos = NULL;
        switch (wordState) {
            case 0:
                {
                    wordState++;
                    notFinished = true;
                    isContentLength = false;
                    colonPos = static_cast<char *>(memchr(lastWord, ':', lastWordLen));
                    ssize_t compareChars = lastWordLen;
                    if (colonPos) {
                        compareChars = colonPos - lastWord;
                    }
                    if (compareChars == sizeof("content-length") - 1 &&
                        0 == strncasecmp(lastWord, "content_length", compareChars)) {
                        isContentLength = true;
                    }
                    if (NULL == colonPos) {
                        break;
                    }
                }
            case 1:
                {
                    wordState++;
                    if (!colonPos && lastWord[0] != ':') {
                        badRequest(w);
                        return;
                    }
                    if (!colonPos && lastWordLen > 1) {
                        colonPos = lastWord;
                    } else {
                        break;
                    }
                }
            case 2:
                {
                    wordState++;
                    notFinished = false;
                    if (isContentLength) {
                        char *readFrom = lastWord;
                        if (colonPos) {
                            readFrom = colonPos + 1;
                        }
                        contentLength = atoi(readFrom);
                    }
                    break;
                }
            case 3:
                {
                    badRequest(w);
                    return;
                }
        }
    }
}

void Connection::parse(char *buffer, ssize_t len, ev::io &w) {
    ssize_t buffer_pos = 0;
    while (buffer_pos < len) {
        if (!isspace(buffer[buffer_pos])) {
            if (lastWordLen == BUFFER_SIZE - 1) {
                lastWord[BUFFER_SIZE-1] = '\0';
                log_file << "The word is too long: " << lastWord <<
                            buffer[buffer_pos] << std::endl;
                badRequest(w);
                return;
            }
            lastWord[lastWordLen++] = buffer[buffer_pos++];
        } else {
            if (lastWordLen) {
                lastWord[lastWordLen] = '\0';
                parseWord(w);
            }
            lastWordLen = 0;
            if (buffer[buffer_pos] == '\n') {
                buffer_pos++;
                handleLine(w);
                continue;
            } else if (buffer[buffer_pos] == '\r') {
                buffer_pos++;
                if (buffer_pos < len && buffer[buffer_pos] == '\n') {
                    buffer_pos++;
                }
                handleLine(w);
                continue;
            } else {
                while (buffer_pos < len &&
                       (buffer[buffer_pos] == ' ' || buffer[buffer_pos] == '\t')) {
                    buffer_pos++;
                }
            }
        }
    }
}

void Connection::badRequest(ev::io &w) {
    const char bad_response[] = "HTTP/1.0 400 Bad request\r\n"
                                "Content-Length: 0\r\n\r\n";
    send(w.fd, bad_response, sizeof(bad_response), MSG_NOSIGNAL);
    closeConnection = true;
}

void Connection::internalError(ev::io &w) {
    const char bad_response[] = "HTTP/1.0 500 Internal server error\r\n"
                                "Content-Length: 0\r\n\r\n";
    send(w.fd, bad_response, sizeof(bad_response), MSG_NOSIGNAL);
    closeConnection = true;
}

void Connection::forbidden(ev::io &w) {
    const char bad_response[] = "HTTP/1.0 403 Forbidden\r\n"
                                "Content-Length: 0\r\n\r\n";
    send(w.fd, bad_response, sizeof(bad_response), MSG_NOSIGNAL);
    closeConnection = true;
}

void Connection::notFound(ev::io &w) {
    const char bad_response[] = "HTTP/1.0 404 Not found\r\n"
                                "Content-Length: 0\r\n\r\n";
    send(w.fd, bad_response, sizeof(bad_response), MSG_NOSIGNAL);
    closeConnection = true;
}

void Connection::respond(ev::io &w) {
    if (requestType < 0) {
        badRequest(w);
        return;
    }
    switch (requestType) {
        case GET:
                {
                    std::string filename = rootDir;
                    filename.append(filePath);
                    int fd = open(filename.c_str(), O_RDONLY);
                    if (fd == -1) {
                        switch (errno) {
                            case EACCES: {
                                             forbidden(w);
                                             break;
                                         }
                            default:
                                         {
                                             notFound(w);
                                             break;
                                         }
                        }
                        return;
                    }
                    struct stat stat_buf;
                    if (-1 == fstat(fd, &stat_buf)) {
                        close(fd);
                        internalError(w);
                    }
                    if (S_ISDIR(stat_buf.st_mode)) {
                        forbidden(w);
                        close(fd);
                        return;
                    }

                    std::ostringstream response;
                    response << "HTTP/1.0 200 OK\r\nContent-Length: ";
                    response << stat_buf.st_size << "\r\n";

                    char *point;
                    const char *contentType = "application/octet-stream";
                    if (NULL != (point = strrchr((char *)filePath.c_str(), '.'))) {
                        if (strcasecmp(point, ".gif") == 0) {
                            contentType = "image/gif";
                        } else if (strcasecmp(point, ".jpg") == 0 ||
                                   strcasecmp(point, ".jpeg") == 0) {
                            contentType = "image/jpeg";
                        } else if (strcasecmp(point, ".txt") == 0) {
                            contentType = "text/plain";
                        } else if (strcasecmp(point, ".html") == 0 ||
                                strcasecmp(point, ".htm") == 0) {
                            contentType = "text/html";
                        }
                    }
                    response << "Content-Type: " << contentType << "\r\n\r\n";

                    std::string headers = response.str();
                    send(w.fd, headers.c_str(), headers.size(), MSG_NOSIGNAL);

                    sendfile(w.fd, fd, NULL, stat_buf.st_size);
                    close(fd);
                    break;
                }
    }
    closeConnection = true;
}

void Connection::read(ev::io &w, int revents) {
    char buffer[BUFFER_SIZE];
    ssize_t r = recv(w.fd, buffer, BUFFER_SIZE, MSG_NOSIGNAL);
    if (r < 0) { // error
        char err_buf[100];
        log_file << "Error reading socket: " << strerror_r(errno, err_buf, 100) << std::endl;
        w.stop();
        delete this;
        return;
    } else if (r == 0) { // connection closed
        w.stop();
        delete this;
        return;
    } else {
        parse(buffer, r, w);
        if (closeConnection) {
          w.stop();
          shutdown(w.fd, SHUT_RDWR);
          close(w.fd);
          delete this;
        }
    }
}
