#pragma once

class NetAddress {
public:
    // Constructors
    NetAddress() = default;
    NetAddress(const std::string& ip, uint16_t port);
    NetAddress(const asio::ip::tcp::endpoint& endpoint);

    // Getters
    const asio::ip::tcp::endpoint& GetEndpoint() const { return _endpoint; }
    std::string GetIPAddress() const;
    uint16_t GetPort() const;

    // Utility methods
    static NetAddress FromEndpoint(const asio::ip::tcp::endpoint& endpoint);
    static NetAddress Any(uint16_t port);

    // Operators
    bool operator==(const NetAddress& other) const;
    bool operator!=(const NetAddress& other) const;

private:
    asio::ip::tcp::endpoint _endpoint;
};