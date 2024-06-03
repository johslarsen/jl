#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("unique_socket") {
  TEST_CASE("sockaddr") {
    auto pipe = jl::unique_socket(socket(AF_UNIX, SOCK_STREAM, 0));
    auto ipv4 = jl::unique_socket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    auto ipv6 = jl::unique_socket(socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP));
    CHECK(":0" == jl::host_port::from(pipe.fd()).string());
    CHECK("0.0.0.0:0" == jl::host_port::from(ipv4.fd()).string());
    CHECK("[::]:0" == jl::host_port::from(ipv6.fd()).string());
  }
  TEST_CASE("pipes") {
    auto [in, out] = jl::unique_socket::pipes();

    CHECK(3 == jl::send(*out, std::string_view("foo")));

    std::string buffer = "xxxx";
    CHECK("foo" == jl::recv(*in, buffer));
  }

  TEST_CASE("UDP") {
    auto receiver = jl::unique_socket::udp();
    auto port = jl::host_port::from(receiver.fd()).port;
    auto sender = jl::unique_socket::udp();
    jl::connect(*sender, {"127.0.0.1", std::to_string(port)});

    CHECK(3 == jl::send(*sender, "foo"));
    CHECK(3 == jl::send(*sender, "bar"));
    std::string buffer = "xxxxxxx";
    CHECK("foo" == jl::recv(*receiver, buffer, MSG_DONTWAIT));
    CHECK("bar" == jl::recv(*receiver, buffer, MSG_DONTWAIT));
  }

  TEST_CASE("TCP") {
    auto server = jl::unique_socket::tcp();
    jl::listen(*server, 2);
    auto client = jl::unique_socket::tcp();
    jl::connect(*client, jl::type_erased_sockaddr::from(server.fd()));

    auto accepted = jl::accept(*server);
    REQUIRE(accepted.has_value());  // NOLINTNEXTLINE(*unchecked-optional*)
    auto& [receiver, addr] = *accepted;
    CHECK("::1" == addr.host);

    SUBCASE("send/recv") {
      CHECK(3 == jl::send(*client, "foo"));
      CHECK(3 == jl::send(*client, "bar"));

      std::string buffer = "xxxxxxx";
      CHECK("foobar" == jl::recv(*receiver, buffer, MSG_DONTWAIT));
    }

    SUBCASE("RST") {
      CHECK(0 == std::move(receiver).terminate().size());

      CHECK(-1 == send(client.fd(), "", 0, 0));
      CHECK(ECONNRESET == errno);
    }

    SUBCASE("poll after FIN") {
      CHECK(0 == jl::poll(receiver.fd(), POLLIN | POLLRDHUP));
      client.reset();
      CHECK((POLLIN | POLLRDHUP) == jl::poll(receiver.fd(), POLLIN | POLLRDHUP));
    }

    SUBCASE("poll after RST") {
      std::move(client).terminate();
      CHECK((POLLIN | POLLERR | POLLHUP | POLLRDHUP) == jl::poll(receiver.fd(), POLLIN | POLLRDHUP));
    }
  }

  TEST_CASE("both IPv4 and IPv6 sockets can be default bound") {
    jl::bind(*jl::unique_socket(socket(AF_INET, SOCK_DGRAM, 0)));
    jl::bind(*jl::unique_socket(socket(AF_INET, SOCK_STREAM, 0)));
    jl::bind(*jl::unique_socket(socket(AF_INET6, SOCK_DGRAM, 0)));
    jl::bind(*jl::unique_socket(socket(AF_INET6, SOCK_STREAM, 0)));
  }

  TEST_CASE("recv and send works with various inputs") {
    auto [in, out] = jl::unique_socket::pipes();
    std::vector<char> char_vector = {'f', 'o', 'o'};
    std::string string = "bar";
    std::vector<int> int_vector = {1, 2, 3};

    CHECK(3 == jl::send(*out, std::span<char>(char_vector)));
    CHECK(3 == jl::send(*out, string));
    CHECK(3 == jl::send(*out, int_vector));

    auto foo = jl::recv(*in, char_vector);
    CHECK("foo" == jl::view_of(foo));
    CHECK("bar" == jl::recv(*in, string));
    auto int123 = jl::recv(*in, std::span<int>(int_vector));
    CHECK((std::vector<int>{1, 2, 3}) == std::vector<int>(int123.begin(), int123.end()));
  }
}

TEST_SUITE("mmsg_buffer") {
  TEST_CASE("datagrams") {
    auto [a, b] = jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM);
    jl::mmsg_buffer<char> sender(std::move(a), 3);
    jl::mmsg_buffer<char> receiver(std::move(b), 3);

    sender.write(0, std::string_view("foo"));
    sender.write(1, std::string_view("bar"));
    CHECK(2 == sender.sendmmsg(0, 2));

    auto msgs = receiver.recvmmsg();
    REQUIRE(2 == msgs.size());
    CHECK("foo" == jl::view_of(msgs[0]));
    CHECK("bar" == jl::view_of(msgs[1]));
  }

  TEST_CASE("stream") {
    auto [sender, b] = jl::unique_socket::pipes();
    jl::mmsg_buffer<char> receiver(std::move(b), 3);

    CHECK(3 == jl::send(*sender, "foo"));
    CHECK(3 == jl::send(*sender, "bar"));

    auto msgs = receiver.recvmmsg();
    REQUIRE(1 == msgs.size());
    CHECK("foobar" == jl::view_of(msgs[0]));
  }
}
