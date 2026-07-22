#pragma once
//
// server.hpp — a small TCP server that exposes the engine over the network.
//
// Model: one thread per connected client (simple and easy to explain). Every
// client thread calls into the SAME Engine, and the Engine's single mutex makes
// those concurrent calls safe. Using a bounded "thread pool" instead of one
// thread per client is the standard optimization — discussed in the study
// guide, not built here.
//
#include "engine.hpp"
#include <cstdint>
#include <string>

// A socket handle. On POSIX it's an int file descriptor; on Windows it's the
// SOCKET type, an unsigned pointer-sized handle. We alias it here — using
// uintptr_t on Windows (which is what SOCKET actually is) so this header does
// NOT have to include the heavy <winsock2.h>.
#ifdef _WIN32
using socket_t = std::uintptr_t;
#else
using socket_t = int;
#endif

namespace ledgerdb {

class Server {
public:
    Server(Engine& engine, int port);
    // Blocks forever, accepting clients. Ctrl-C to stop.
    void run();

private:
    void handle_client(socket_t client_fd);   // one client's whole conversation
    std::string handle_line(const std::string& line);  // one command -> reply

    Engine& engine_;
    int      port_;
};

} // namespace ledgerdb
