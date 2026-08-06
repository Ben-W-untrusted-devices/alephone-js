/*
    Copyright (C) 2024 Benoit Hauquier and the "Aleph One" developers.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    This license is contained in the file "COPYING",
    which is included with this source code; it is available online at
    http://www.gnu.org/licenses/gpl.html
*/

#ifndef NETWORK_INTERFACE_H
#define NETWORK_INTERFACE_H

// DISABLE_NETWORKING lives in the generated config.h; make sure it's visible
// here regardless of what the includer has pulled in already (see
// ../../WEB_PORT_PLAN.md, M3).
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#if !defined(DISABLE_NETWORKING)
#include <asio.hpp>

class IPaddress {
private:
    friend class NetworkInterface;
    friend class UDPsocket;
    friend class TCPsocket;

    asio::ip::address _address;
    uint16_t _port = 0;

    IPaddress(const asio::ip::tcp::endpoint& endpoint);
    IPaddress(const asio::ip::udp::endpoint& endpoint);

public:
    IPaddress(const std::string& host, uint16_t port);
    IPaddress(const uint8_t ip[4], uint16_t port);
    IPaddress() = default;
    bool is_v4() const { return _address.is_v4(); }
    std::string address() const { return _address.to_string(); }
    std::array<unsigned char, 4> address_bytes() const { return _address.to_v4().to_bytes(); }
    uint16_t port() const { return _port; }
    void set_port(uint16_t port);
    void set_address(const std::string& host);
    void set_address(const uint8_t ip[4]);

    bool operator==(const IPaddress& other) const;
    bool operator!=(const IPaddress& other) const;
};
#else
// Web port (see WEB_PORT_PLAN.md, M3): a browser has no raw socket API at
// all, so real networking can't function here regardless of whether asio
// itself compiles. This and the other stub classes below keep the same
// public shape as the real ones (trivial/no-op bodies, all inline -- no
// matching .cpp needed) so that code which merely references these types,
// without expecting working networking, keeps compiling unchanged.
class IPaddress {
public:
    IPaddress(const std::string& host, uint16_t port) {}
    IPaddress(const uint8_t ip[4], uint16_t port) {}
    IPaddress() = default;
    bool is_v4() const { return true; }
    std::string address() const { return std::string(); }
    std::array<unsigned char, 4> address_bytes() const { return {0, 0, 0, 0}; }
    uint16_t port() const { return 0; }
    void set_port(uint16_t port) {}
    void set_address(const std::string& host) {}
    void set_address(const uint8_t ip[4]) {}

    bool operator==(const IPaddress& other) const { return true; }
    bool operator!=(const IPaddress& other) const { return false; }
};
#endif // !defined(DISABLE_NETWORKING)

/* missing from AppleTalk.h */
// ZZZ: note that this determines only the amount of storage allocated for packets, not
// the size of actual packets sent.  I believe UDP on Ethernet should be able to carry
// around 1.5K per packet, not sure of the exact figure off the top of my head though.
#define ddpMaxData 1500

struct UDPpacket
{
    IPaddress address;
    std::array<uint8_t, ddpMaxData> buffer;
    int data_size;
};

#if !defined(DISABLE_NETWORKING)
class UDPsocket {
private:
    asio::io_context& _io_context;
    asio::ip::udp::socket _socket;
    asio::ip::udp::endpoint _receive_async_endpoint;
    int64_t _receive_async_return_value = 0;
    UDPsocket(asio::io_context& io_context, asio::ip::udp::socket&& socket);
    friend class NetworkInterface;
public:
    int64_t broadcast_send(const UDPpacket& packet);
    int64_t send(const UDPpacket& packet);
    int64_t receive(UDPpacket& packet);
    void register_receive_async(UDPpacket& packet);
    int64_t receive_async(int timeout_ms);
    bool broadcast(bool enable);
    int64_t check_receive() const;
};

class TCPsocket {
private:
    asio::io_context& _io_context;
    asio::ip::tcp::socket _socket;
    TCPsocket(asio::io_context& io_context, asio::ip::tcp::socket&& socket);
    friend class NetworkInterface;
    friend class TCPlistener;
public:
    int64_t send(uint8_t* buffer, size_t size);
    int64_t receive(uint8_t* buffer, size_t size);
    IPaddress remote_address() const { return IPaddress(_socket.remote_endpoint()); }
    bool set_non_blocking(bool enable);
};

class TCPlistener {
private:
    asio::io_context& _io_context;
    asio::ip::tcp::acceptor _acceptor;
    asio::ip::tcp::socket _socket;
    TCPlistener(asio::io_context& io_context, const asio::ip::tcp::endpoint& endpoint);
    friend class NetworkInterface;
public:
    std::unique_ptr<TCPsocket> accept_connection();
    bool set_non_blocking(bool enable);
};

class NetworkInterface {
private:
    asio::io_context _io_context;
    asio::ip::tcp::resolver _resolver;
public:
    NetworkInterface();
    std::unique_ptr<UDPsocket> udp_open_socket(uint16_t port);
    std::unique_ptr<TCPsocket> tcp_connect_socket(const IPaddress& address);
    std::unique_ptr<TCPlistener> tcp_open_listener(uint16_t port);
    std::optional<IPaddress> resolve_address(const std::string& host, uint16_t port);
};
#else
class UDPsocket {
public:
    int64_t broadcast_send(const UDPpacket& packet) { return -1; }
    int64_t send(const UDPpacket& packet) { return -1; }
    int64_t receive(UDPpacket& packet) { return -1; }
    void register_receive_async(UDPpacket& packet) {}
    int64_t receive_async(int timeout_ms) { return -1; }
    bool broadcast(bool enable) { return false; }
    int64_t check_receive() const { return -1; }
};

class TCPsocket {
public:
    int64_t send(uint8_t* buffer, size_t size) { return -1; }
    int64_t receive(uint8_t* buffer, size_t size) { return -1; }
    IPaddress remote_address() const { return IPaddress(); }
    bool set_non_blocking(bool enable) { return false; }
};

class TCPlistener {
public:
    std::unique_ptr<TCPsocket> accept_connection() { return nullptr; }
    bool set_non_blocking(bool enable) { return false; }
};

class NetworkInterface {
public:
    NetworkInterface() = default;
    std::unique_ptr<UDPsocket> udp_open_socket(uint16_t port) { return nullptr; }
    std::unique_ptr<TCPsocket> tcp_connect_socket(const IPaddress& address) { return nullptr; }
    std::unique_ptr<TCPlistener> tcp_open_listener(uint16_t port) { return nullptr; }
    std::optional<IPaddress> resolve_address(const std::string& host, uint16_t port) { return std::nullopt; }
};
#endif // !defined(DISABLE_NETWORKING)

#endif // NETWORK_INTERFACE_H
