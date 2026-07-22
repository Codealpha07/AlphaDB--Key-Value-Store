//
// main.cpp — wire everything together and start serving.
//
// Usage:
//   ledgerdb [data_file] [port]
//
// Defaults to ./ledgerdb.data on port 5555.
//
#include "engine.hpp"
#include "server.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string data_file = (argc > 1) ? argv[1] : "ledgerdb.data";
    int         port      = (argc > 2) ? std::stoi(argv[2]) : 5555;

    try {
        ledgerdb::Engine engine(data_file, /*fsync_on_write=*/true);
        std::cout << "Opened '" << data_file << "' with "
                  << engine.size() << " live keys.\n";

        ledgerdb::Server server(engine, port);
        server.run();  // blocks forever
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
