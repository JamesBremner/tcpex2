#include <string>
#include <iostream>
#include <thread>

#include "cTCPex.h"

#define VERBOSE

namespace raven
{
    namespace set
    {

        cTCPex::cTCPex()
            : myFrameLines(true)
        {
            myConnectSocket.resize(1, INVALID_SOCKET);
        }

        bool cTCPex::connect_to_server_wait(
            const std::string &server_address,
            const std::string &server_port,
            int timoutSecs)
        {
            // loop until connection made
            int count = 0;
            while (1)
            {
                if (connect_to_server(
                        server_address,
                        server_port))
                    break;

                // connection failed, wait 1 second then try again
                std::this_thread::sleep_for(
                    std::chrono::seconds(1));

                // check for timeout
                if (timoutSecs)
                {
                    if (count++ > timoutSecs)
                        return false;
                }
            }
            return true;
        }

        bool cTCPex::connect_to_server(
            const std::string &server_address,
            const std::string &server_port)
        {

            if (server_port.empty())
                throw std::runtime_error("Server not configured");

            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData))
            {
                throw std::runtime_error("Winsock init failed");
            }

            struct addrinfo *result = NULL,
                            hints;

            ZeroMemory(&hints, sizeof(hints));
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            if (getaddrinfo(
                    server_address.c_str(),
                    server_port.c_str(),
                    &hints, &result))
            {
                throw std::runtime_error("getaddrinfo failed");
            }

            myConnectSocket[0] = ::socket(
                result->ai_family,
                result->ai_socktype,
                result->ai_protocol);
            if (myConnectSocket[0] == INVALID_SOCKET)
            {
                throw std::runtime_error("socket failed");
            }

            // std::cout << "attempting connect\n";
            int connect_return;
            try
            {
                connect_return = ::connect(
                    myConnectSocket[0],
                    result->ai_addr,
                    (int)result->ai_addrlen);
            }
            catch (std::runtime_error &e)
            {
                std::cout << server_address << ":" << myServerPort
                          << " socket connect threw exception";
                connect_return = SOCKET_ERROR;
            }
            if (connect_return == SOCKET_ERROR)
            {
                int err = WSAGetLastError();
                closesocket(myConnectSocket[0]);
                myConnectSocket[0] = INVALID_SOCKET;
                if (err == 10060)
                {
                    std::cout << "connect timed out\n";
                    return false;
                }
                else if (err == 10061)
                {
                    std::cout << server_address << ":" << myServerPort
                              << " No connection could be made because the target machine actively refused it. "
                                 " Generally, it happens that something is preventing a connection to the port or hostname. "
                                 " Either there is a firewall blocking the connection "
                                 " or the process that is hosting the service is not listening on that specific port.\n";
                    return false;
                }
                else
                    std::cout << "connect failed error: " << std::to_string(err);
                return false;
            }

            // wait for messages from server in a new thread.
            std::thread t(readBlock, this, 0);
            t.detach();

            return true;
        }

        void cTCPex::start_server(
            const std::string &ServerPort,
            int maxClient)
        {
            myServerPort = ServerPort;
            myConnectSocket.resize(maxClient, INVALID_SOCKET);

            // construct socket where server listens for client connection requests
            acceptSocketCtor();

            /* start thread that listens for client connection requests

            The thread keeps running after this method returns
            Each time a connection is requested the thread runs acceptBlock()

            Meanwhile this method returns to caller
            */

            std::thread t(acceptBlock, this);
            t.detach();
        }

        void cTCPex::acceptBlock()
        {
            while (true)
            {
                // wait for client connection request
                int client = acceptClientMultiple();
                if (client < 0)
                {
                    // a problem occurred
                    // sleep, then try again
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(25));
                    continue;
                }

                // good connection request
                // handle it in its own, new thread
                std::cout << "Accepted client " << client << "\n";
                std::thread t(readBlock, this, client);
                t.detach();

                // loop to listen for next connection request
            }
        }
        void cTCPex::readBlock(
            int client)
        {
            static sEvent e;
            e.client = client;
            e.type = eEvent::accept;
            e.msg = "";
            eventHandler(e);

            while (true)
            {
                // wait for next message from client
                read(client);

                // check for disconnection
                if (!isConnected(client))
                {
                    // invoke handler with disconnect message
                    e.type = eEvent::disconnect;
                    eventHandler(e);

                    // exit ( ends thread )
                    return;
                }

                // get line received if available and option set
                auto line = nextLine(readMsg());
                if (line.empty())
                    continue;

#ifdef VERBOSE
                std::cout << "Client " << client
                          << " sent " << line << "\n";
#endif

                e.client = client;
                e.type = eEvent::read;
                e.msg = line;
                eventHandler(e);
            }
        }

        std::string cTCPex::nextLine(const std::string &msg)
        {
            static std::string msg_acc = "";

            if (!myFrameLines)
                return msg;

            for (char c : msg)
            {
                if ((int)((unsigned char)c) == 0xFF)
                {
                    for (char c : msg_acc)
                        std::cout << std::hex << (int)((unsigned char)c) << " ";
                    std::cout << std::endl;
                    std::cout << "garbage received, ignoring it\n";
                    return "";
                }
            }

            msg_acc += msg;

#ifdef VERBOSE
            std::cout << "msg_acc " << msg_acc << "\n";
#endif

            // check for complete valid line
            int p = msg_acc.find_first_of("\n\r");
            if (p == -1)
                return "";
            if (msg_acc.find_first_not_of("\n\r") == -1)
            {
                msg_acc.clear();
                return "";
            }

            // complete line received
            std::string ret = msg_acc.substr(0, p) + "\n";
            msg_acc = msg_acc.substr(
                msg_acc.find_last_of("\n\r") + 1);

#ifdef VERBOSE
            std::cout << "complete line.  msg_acc " << msg_acc << "\n";
#endif
            return ret;
        }

        void cTCPex::initWinSock()
        {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData))
            {
                throw std::runtime_error("Winsock init failed");
            }
        }

        void cTCPex::acceptSocketCtor()
        {
            if (myServerPort.empty())
                throw std::runtime_error(
                    "Server not configured");

            initWinSock();

            struct addrinfo *result = NULL,
                            hints;

            ZeroMemory(&hints, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            hints.ai_flags = AI_PASSIVE;

            int error = getaddrinfo(
                NULL, myServerPort.c_str(),
                &hints, &result);
            if (error)
            {
                throw std::runtime_error(
                    "getaddrinfo failed " + std::to_string(error));
            }

            myAcceptSocket = ::socket(
                result->ai_family,
                result->ai_socktype,
                result->ai_protocol);
            if (myAcceptSocket == INVALID_SOCKET)
            {
                throw std::runtime_error("socket failed");
            }

            if (::bind(myAcceptSocket,
                       result->ai_addr,
                       (int)result->ai_addrlen) == SOCKET_ERROR)
            {
                closesocket(myAcceptSocket);
                myAcceptSocket = INVALID_SOCKET;
                throw std::runtime_error("bind failed");
            }

            if (::listen(
                    myAcceptSocket,
                    SOMAXCONN) == SOCKET_ERROR)
            {
                closesocket(myAcceptSocket);
                myAcceptSocket = INVALID_SOCKET;
                throw std::runtime_error("listen failed");
            }
        }

        int cTCPex::acceptClientMultiple()
        {
            if (countConnectedClients() >= myConnectSocket.size())
            {
                // maximum clients already connected
                return -2;
            }
            std::cout << "cTCP listening for multiple clients on port "
                      << myServerPort << "\n";

            struct sockaddr_in client_info;
            int size = sizeof(client_info);
            SOCKET s = ::accept(
                myAcceptSocket,
                (sockaddr *)&client_info,
                &size);
            if (s == INVALID_SOCKET)
            {
                std::cout << "invalid socket\n";
                return -1;
            }

            int clientIndex = addConnectedSocket(s);
            myRemoteAddress = inet_ntoa(client_info.sin_addr);

            std::cout << "cTCP client from " << myRemoteAddress
                      << " accepted on " << client_info.sin_port << "\n";

            return clientIndex;
        }

        void cTCPex::eventHandler(
            const sEvent &e)
        {
            std::lock_guard<std::mutex> lck(mtxEventQ);
            myEventQ.push(e);
        }

        cTCPex::sEvent cTCPex::eventPop()
        {
            sEvent e;
            while (1)
            {
                {
                    std::lock_guard<std::mutex> lck(mtxEventQ);

                    if (myEventQ.size())
                    {
                        e = myEventQ.front();
                        myEventQ.pop();
                        return e;
                    }
                }

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100));
            }
        }

        int cTCPex::countConnectedClients()
        {
            int count = 0;
            for (auto s : myConnectSocket)
                if (s != INVALID_SOCKET)
                    count++;
            return count;
        }

        bool cTCPex::isConnected(int client) const
        {
            return myConnectSocket[client] != INVALID_SOCKET;
        }

        int cTCPex::maxClient() const
        {
            return myConnectSocket.size();
        }

        std::string cTCPex::readMsg() const
        {
            return std::string(myReadbuf);
        }

        int cTCPex::addConnectedSocket(SOCKET s)
        {
            for (int kc = 0; kc < myConnectSocket.size(); kc++)
                if (myConnectSocket[kc] == INVALID_SOCKET)
                {
                    myConnectSocket[kc] = s;
                    return kc;
                }
            return -1;
        }
        SOCKET cTCPex::clientSocket(int client)
        {
            if (client < 0)
                return INVALID_SOCKET;
            if (client >= myConnectSocket.size())
                return INVALID_SOCKET;
            return myConnectSocket[client];
        }
        int cTCPex::clientPort(int client)
        {
            if (client < 0)
                return 0;
            if (client >= myConnectSocket.size())
                return 0;
            if (myConnectSocket[client] == INVALID_SOCKET)
                return 0;
            SOCKADDR_IN sa;
            int namelen = sizeof(sa);
            getsockname(
                myConnectSocket[client],
                (struct sockaddr *)&sa,
                &namelen);
            int dbg = sa.sin_port;
            return 0;
        }
        int cTCPex::read(int client)
        {
            // std::cout << "cTCP::read " << client << "\n";
            if (myConnectSocket[client] == INVALID_SOCKET)
            {
                std::cout << "cTCP read on invalid socket\n";
                return -1;
            }

            // clear old message
            ZeroMemory(myReadbuf, 1024);

            // wait to receive message
            int r = ::recv(
                myConnectSocket[client],
                (char *)myReadbuf, 1024, 0);

            // check for message received
            // if no message or error, assume connection closed
            if (r <= 0)
            {
                std::cout << "connection closed\n";
                closesocket(myConnectSocket[0]);
                myConnectSocket[0] = INVALID_SOCKET;
            }
            return client;
        }

        void cTCPex::send(
            const std::string &msg,
            int client)
        {
            if (msg.empty())
            {
#ifdef VERBOSE
                std::cout << "cTCPex:send ignored empty msg\n";
#endif
                return;
            }
            if (myConnectSocket[client] == INVALID_SOCKET)
                throw std::runtime_error("send on invalid socket");
            if (::send(
                    myConnectSocket[client],
                    msg.c_str(),
                    (int)msg.length(), 0) == SOCKET_ERROR)
                std::cout << "cTCPex::send error on "
                          << msg << "\n";
#ifdef VERBOSE
            else
                std::cout << "cTCPex:send sent "
                          << msg << "\n";
#endif
        }

        void cTCPex::send(
            const std::vector<unsigned char> &msg,
            int client)
        {
            if (myConnectSocket[client] == INVALID_SOCKET)
                throw std::runtime_error("send on invalid socket");
            ::send(
                myConnectSocket[client],
                (char *)msg.data(),
                msg.size(), 0);
        }
    }
}
