#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <thread>

#include "exceptions.h"
#include "logging.h"
#include "source.h"

using namespace pensieve::exception;

namespace pensieve::io {

FileSource::FileSource(const std::string& file_name)
: d_file_name(file_name)
{
    d_stream.open(d_file_name, std::ios::binary | std::ios::in);
}

void
FileSource::read(char* stream, ssize_t length)
{
    d_stream.read(stream, length);
    if (!d_stream) {
        throw IoError{"Failed to read file"};
    }
}

void
FileSource::getline(std::string& result, char delimiter)
{
    std::getline(d_stream, result, delimiter);
    if (!d_stream) {
        throw IoError{"Failed to read file"};
    }
}

void
FileSource::close()
{
    _close();
}

void
FileSource::_close()
{
    d_stream.close();
}
bool
FileSource::is_open()
{
    return d_stream.is_open();
}

FileSource::~FileSource()
{
    _close();
}

SocketSource::SocketSource(int port)
{
    struct addrinfo hints = {};
    struct addrinfo* all_addresses = nullptr;
    struct addrinfo* curr_address = nullptr;
    int rv;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    while (curr_address == nullptr) {
        if ((rv = ::getaddrinfo(nullptr, port_str.c_str(), &hints, &all_addresses)) != 0) {
            LOG(ERROR) << "Encountered error in 'getaddrinfo' call: " << ::gai_strerror(rv);
            throw IoError{"Failed to resolve host IP and port"};
        }

        // loop through all the results and connect to the first we can
        for (curr_address = all_addresses; curr_address != nullptr; curr_address = curr_address->ai_next)
        {
            if ((d_sockfd = ::socket(
                         curr_address->ai_family,
                         curr_address->ai_socktype,
                         curr_address->ai_protocol))
                == -1)
            {
                continue;
            }

            if (::connect(d_sockfd, curr_address->ai_addr, curr_address->ai_addrlen) == -1) {
                ::close(d_sockfd);
                continue;
            }
            break;
        }
        if (curr_address == nullptr) {
            freeaddrinfo(all_addresses);
            LOG(DEBUG) << "No connection, sleeping before retrying...";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    freeaddrinfo(all_addresses);
    d_is_open = true;
}

void
SocketSource::read(char* result, ssize_t length)
{
    while (length > 0) {
        ssize_t ret = ::recv(d_sockfd, result, length, 0);
        if (ret > 0) {
            result += ret;
            length -= ret;
        } else if (ret == 0) {
            LOG(DEBUG) << "Connection closed by remote";
            throw IoError{"Connection closed by remote"};
        } else if (ret < 0 && errno != EINTR) {
            LOG(WARNING) << "Encountered error in 'recv' call: " << ::strerror(errno);
            throw IoError{"recv call failed"};
        }
    }
}

void
SocketSource::_close()
{
    if (d_is_open) {
        ::close(d_sockfd);
    }
    d_is_open = false;
}

void
SocketSource::close()
{
    _close();
}

bool
SocketSource::is_open()
{
    return d_is_open;
}

void
SocketSource::getline(std::string& result, char delimiter)
{
    char buf;
    while (true) {
        read(&buf, 1);
        if (buf == delimiter) {
            break;
        }
        result.push_back(buf);
    }
}

SocketSource::~SocketSource()
{
    _close();
}

}  // namespace pensieve::io