#include <iostream>
#include <thread>

// Client executable: sends packets to matching engine and receives responses
int main()
{
    int return_code = 0;
    std::cout << "Peregrine Client\n";
    std::cout << "Make sure to start server before starting client.";
    std::cout << "Connecting to matching engine...\n";

    // TODO: Implement:
    // - AF_XDP network ingestion
    // - Packet generation and transmission
    // - Response reception and processing
    // - Latency measurement with TSC
    // - Integration with matching engine

    return return_code;
}
