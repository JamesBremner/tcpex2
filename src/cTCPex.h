#include <winsock2.h>
#include <string>
#include <vector>
#include <functional>
#include <queue>
#include <mutex>
#include <ws2tcpip.h>

namespace raven
{
    namespace set
    {

        /// A C++ wrapper for windows TCP sockets

        class cTCPex
        {
        public:
            enum class eEvent
            {
                none,
                accept,
                read,
                disconnect,
            };

            struct sEvent
            {
                int client;         // index of client generating event
                eEvent type;        // event type
                std::string msg;    // msg that generated ( read ) event
            };

            cTCPex();

            /** Connect to server
            /// @param server_address 
            /// @param server_port 
            /// @return true if connected

            This blocks until the server accepts the connection, or rejects the connection

            If connection succesful, a new thread is started that waits for messages from the server.
            The thread runs until the app exits - app code must keep the app runing.
            When a message arrives, the handler runs in this thread.
            */

            bool connect_to_server(
                const std::string &server_address,
                const std::string &server_port);

            /** Connect to server, block until success
            /// @param server_address 
            /// @param server_port 
            /// @param timoutSecs 0 for wait forever ( 0 is default )
            /// @return true if succesful
            */

            bool connect_to_server_wait(
                const std::string &server_address,
                const std::string &server_port,
                int timoutSecs = 0 );

            /** start server

             @param[in] ServerPort listens for clients
             @param[in] eventHandler handler to call when something happens
             @param[in] maxClient max number of clients, default 1

             Starts listening for client connection requests.

             The eventHandler function will be called when
             - a client connects
             - a message is received from client
             - a client diconnects
             and runs in the thread connected to the client

             eventHandler signature:  std::string h( const sEvent& event )
  
             client the index among connected clients of client that sent the message
             type of event
             msg the message received from the client

             Threading

            When the server starts,
                - a new thread is started that listens for connection requests.
                    runs until the application exits
                - a new thread is started that checks for jobs waiting to be processed
                    runs until the application exits
            When a new client connects,
                - a new thread is started that listens to the client.
                    This thread runs until the client disconnects
                    The eventHandler runs in this thread


             This method returns immediatly, leaving the client connect listening thread running.

             */

            void start_server(
                const std::string &ServerPort,
                int maxClient = 1);

            /// Send message to client
            void send(
                const std::string &msg,
                int client = 0);
            void send(
                const std::vector<unsigned char> &msg,
                int client = 0);

            /// Get last message from peer
            std::string readMsg() const;

            /// True if peer connected
            bool isConnected(int client = 0) const;

            int countConnectedClients();

            int maxClient() const;

            /** set line accumulation option
             * @param f false for instant processing of everthing received from client
             * @param f true wait to process input until complete line received
             */

            void lineAccumulator(bool f)
            {
                myFrameLines = f;
            }

            sEvent eventPop();

        private:
            bool myFrameLines;
            bool mySharedProcessingThread;
            std::string myServerPort;
            SOCKET myAcceptSocket;               ///< socket listening for clients
            std::vector<SOCKET> myConnectSocket; ///< sockets connected to clients
            std::string myRemoteAddress;
            char myReadbuf[1024];       
            std::queue<sEvent> myEventQ;  ///< Queue of jobs waiting to run in SharedProcessingThread
            std::mutex mtxEventQ;     ///< protect job queue from multi-thread access

            void initWinSock();

            int addConnectedSocket(SOCKET s);

            /** Wait for client connection requests
             *
             * runs in its own thread
             *
             * Never returns
             */
            void acceptBlock();

            /** Wait for messages from client
             *
             * runs in its own thread, invoking the read handler when a message is recvd
             *
             * Does not return until the client disconnects
             */
            void readBlock(int client);

            void eventHandler(
                const sEvent& e );

            /// Wait for client connection request
            int acceptClientMultiple();

            /// Wait for message from peer
            int read(int client = 0);

            /// @brief Construct socket that listens for client connetion requests
            void acceptSocketCtor();

            /** Accumulate complete lines ( terminated by \n or \r or \n\r )
             * @param msg message received
             * @return line received, "" if no line available
             */
            std::string nextLine(const std::string &msg);

            int clientPort(int client);

            SOCKET clientSocket(int client);


        };

    }
}
