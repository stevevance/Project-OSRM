/*

Copyright (c) 2013, Project OSRM, Dennis Luxen, others
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef SERVER_H
#define SERVER_H

#include "../Util/StringUtil.h"

#include "Connection.h"
#include "RequestHandler.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <functional>
#include <memory>
#include <thread>
#include <vector>

class Server
{
  public:
    explicit Server(const std::string &address, const int port, const unsigned thread_pool_size)
        : thread_pool_size(thread_pool_size), acceptor(io_service),
          new_connection(new http::Connection(io_service, request_handler)), request_handler()
    {
        const std::string port_string = IntToString(port);

        boost::asio::ip::tcp::resolver resolver(io_service);
        boost::asio::ip::tcp::resolver::query query(address, port_string);
        boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve(query);

        acceptor.open(endpoint.protocol());
        acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen();
        acceptor.async_accept(
            new_connection->socket(),
            boost::bind(&Server::HandleAccept, this, boost::asio::placeholders::error));
    }

    // Server() = delete;
    // Server(const Server &) = delete;

    void Run()
    {
        std::vector<std::shared_ptr<std::thread>> threads;
        for (unsigned i = 0; i < thread_pool_size; ++i)
        {
            std::shared_ptr<std::thread> thread = std::make_shared<std::thread>(
                boost::bind(&boost::asio::io_service::run, &io_service));
            threads.push_back(thread);
        }
        for (unsigned i = 0; i < threads.size(); ++i)
        {
            threads[i]->join();
        }
    }

    void Stop() { io_service.stop(); }

    RequestHandler &GetRequestHandlerPtr() { return request_handler; }

  private:
    void HandleAccept(const boost::system::error_code &e)
    {
        if (!e)
        {
            new_connection->start();
            new_connection.reset(new http::Connection(io_service, request_handler));
            acceptor.async_accept(
                new_connection->socket(),
                boost::bind(&Server::HandleAccept, this, boost::asio::placeholders::error));
        }
    }

    unsigned thread_pool_size;
    boost::asio::io_service io_service;
    boost::asio::ip::tcp::acceptor acceptor;
    std::shared_ptr<http::Connection> new_connection;
    RequestHandler request_handler;
};

#endif // SERVER_H
