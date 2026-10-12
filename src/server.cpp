#include "server.hpp"

#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using ssize_t = long long;   // socket_t comes from server.hpp

static inline int sock_close(socket_t s) { return ::closesocket(s); }
static inline ssize_t sock_read(socket_t s, char* buf, size_t len) {
    return ::recv(s, buf, static_cast<int>(len), 0);
}
static inline ssize_t sock_write(socket_t s, const char* buf, size_t len) {
    return ::send(s, buf, static_cast<int>(len), 0);
}
#define IS_INVALID_SOCKET(s) ((s) == INVALID_SOCKET)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
static inline int sock_close(socket_t s) { return ::close(s); }   // socket_t comes from server.hpp
static inline ssize_t sock_read(socket_t s, char* buf, size_t len) {
    return ::read(s, buf, len);
}
static inline ssize_t sock_write(socket_t s, const char* buf, size_t len) {
    return ::write(s, buf, len);
}
#define IS_INVALID_SOCKET(s) ((s) < 0)
#endif

namespace alphadb {

Server::Server(Engine& engine, int port) : engine_(engine), port_(port) {}

// ---------------------------------------------------------------------------
// The text protocol (simple and testable by hand with `nc localhost 5555`):
//
//   SET <key> <value...>   -> +OK
//   GET <key>              -> the value, or (nil)
//   DEL <key>              -> +OK, or (nil) if the key wasn't there
//   PING                   -> +PONG
//
// <value...> is the rest of the line, so values may contain spaces.
// ---------------------------------------------------------------------------
std::string Server::handle_line(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    for (auto& c : cmd) c = static_cast<char>(std::toupper(c));

    if (cmd == "PING") {
        return "+PONG\r\n";
    }
    if (cmd == "GET") {
        std::string key;
        if (!(iss >> key)) return "-ERR usage: GET <key>\r\n";
        auto val = engine_.get(key);
        return val ? (*val + "\r\n") : "(nil)\r\n";
    }
    if (cmd == "DEL") {
        std::string key;
        if (!(iss >> key)) return "-ERR usage: DEL <key>\r\n";
        return engine_.del(key) ? "+OK\r\n" : "(nil)\r\n";
    }
    if (cmd == "SET") {
        std::string key;
        if (!(iss >> key)) return "-ERR usage: SET <key> <value>\r\n";
        std::string value;
        std::getline(iss, value);                       // rest of the line
        if (!value.empty() && value[0] == ' ') value.erase(0, 1);  // drop one leading space
        engine_.put(key, value);
        return "+OK\r\n";
    }
    return "-ERR unknown command '" + cmd + "'\r\n";
}

// ---------------------------------------------------------------------------
// One client connection: read bytes, split into lines, answer each line.
// ---------------------------------------------------------------------------
void Server::handle_client(socket_t client_fd) {
    std::string buffer;
    char chunk[4096];

    while (true) {
        ssize_t n = sock_read(client_fd, chunk, sizeof(chunk));
        if (n <= 0) break;                 // client closed the connection
        buffer.append(chunk, static_cast<std::size_t>(n));

        // A command ends at a newline. There may be several per read, or a
        // partial one left over for the next read. This is "framing".
        std::size_t nl;
        while ((nl = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();  // handle CRLF
            if (line.empty()) continue;

            std::string reply = handle_line(line);
            if (sock_write(client_fd, reply.data(), reply.size()) < 0) { sock_close(client_fd); return; }
        }
    }
    sock_close(client_fd);
}

// ---------------------------------------------------------------------------
// The accept loop: create a listening socket, then hand each new client to
// its own thread.
// ---------------------------------------------------------------------------
void Server::run() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    socket_t listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (IS_INVALID_SOCKET(listen_fd)) throw std::runtime_error("socket() failed");

    int opt = 1;  // let us restart the server without waiting for the OS to
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));  // release the port

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port_));

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed (is the port already in use?)");
    if (::listen(listen_fd, 16) < 0)
        throw std::runtime_error("listen() failed");

    std::cout << "AlphaDB listening on port " << port_ << " ...\n";

    while (true) {
        socket_t client_fd = ::accept(listen_fd, nullptr, nullptr);
        if (IS_INVALID_SOCKET(client_fd)) continue;
        // Detach: the thread cleans itself up when the client disconnects.
        std::thread(&Server::handle_client, this, client_fd).detach();
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

} // namespace alphadb
