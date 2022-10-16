#include "cCommandParser.h"
#include "cTCPex.h"
#include "await.h"

raven::await::cAwait waiter;

std::pair<std::string, std::string> parse_command_line_options(int argc, char *argv[])
{
    std::pair<std::string, std::string> ret;

    raven::set::cCommandParser P;
    P.add("a", "address of server");
    P.add("p", "port where server listens for clients");

    P.parse(argc, argv);

    ret.first = P.value("a");
    ret.second = P.value("p");

    if (ret.first.empty() || ret.second.empty())
    {
        P.describe();
        exit(1);
    }
    return ret;
}

void keylines(raven::set::cTCPex &tcpex)
{
    waiter.repeat(
        [&]
        {
    char name[1024];
    std::string myString;
    std::cin.getline(name, 1024);
    myString = name + std::string("\n");
    tcpex.send(myString);
        },
        [&]{});
}

void events(raven::set::cTCPex &tcpex)
{
    waiter.repeat(
        [&]
        {
            auto e = tcpex.eventPop();
            switch (e.type)
            {
            case raven::set::cTCPex::eEvent::none:
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100));
                break;
            case raven::set::cTCPex::eEvent::accept:
                std::cout << "Connected\n";
                break;
            case raven::set::cTCPex::eEvent::read:
                std::cout << "\nfrom server: " << e.msg << "\n";
                break;
            }
        },
        [] {});
}

main(int argc, char *argv[])
{
    auto server = parse_command_line_options(argc, argv);

    raven::set::cTCPex tcpex;

    tcpex.connect_to_server_wait(
        server.first,
        server.second);

    keylines(tcpex);

    events(tcpex);

    waiter.run();

    return 0;
}
