//
// main.cpp — wire everything together and start serving.
//
// Usage:
//   alphadb [data_file] [port]
//
// Defaults to ./alphadb.data on port 5555.
//
#include "engine.hpp"
#include "server.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string data_file = (argc > 1) ? argv[1] : "alphadb.data";
    int         port      = (argc > 2) ? std::stoi(argv[2]) : 5555;

    try {
        alphadb::Engine engine(data_file, /*fsync_on_write=*/true);
        std::cout << "Opened '" << data_file << "' with "
                  << engine.size() << " live keys.\n";

        alphadb::Server server(engine, port);
        server.run();  // blocks forever
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
