#include <doctest/doctest.h>
#include <jl.h>

static inline std::string_view as_view(std::span<char> data) {
  return {data.begin(), data.end()};
}

TEST_SUITE("unique_socket") {
  TEST_CASE("pipes") {
    auto [in, out] = jl::unique_socket::pipes();

    CHECK(3 == jl::send(*out, std::string_view("foo")));

    std::string buffer = "xxxx";
    CHECK("foo" == jl::recv(*in, buffer));
  }

  TEST_CASE("UDP") {
    auto receiver = jl::unique_socket::udp({"::", "4321"});
    auto sender = jl::unique_socket::udp();
    jl::connect(*sender, {"127.0.0.1", "4321"});

    CHECK(3 == jl::send(*sender, "foo"));
    CHECK(3 == jl::send(*sender, "bar"));
    std::string buffer = "xxxxxxx";
    CHECK("foo" == jl::recv(*receiver, buffer, MSG_DONTWAIT));
    CHECK("bar" == jl::recv(*receiver, buffer, MSG_DONTWAIT));
  }

  TEST_CASE("TCP") {
    auto server = jl::unique_socket::tcp({"::", "4321"});
    jl::listen(*server, 2);

    auto sender = jl::unique_socket::tcp();
    jl::connect(*sender, {"::1", "4321"});

    CHECK(3 == jl::send(*sender, "foo"));
    CHECK(3 == jl::send(*sender, "bar"));

    auto accepted = jl::accept(*server);
    REQUIRE(accepted.has_value());  // NOLINTNEXTLINE(*unchecked-optional*)
    auto& [receiver, addr] = *accepted;
    CHECK("::1" == addr.host);
    std::string buffer = "xxxxxxx";
    CHECK("foobar" == jl::recv(*receiver, buffer, MSG_DONTWAIT));
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
    CHECK("foo" == std::string_view(foo.begin(), foo.end()));
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
    CHECK("foo" == as_view(msgs[0]));
    CHECK("bar" == as_view(msgs[1]));
  }

  TEST_CASE("stream") {
    auto [sender, b] = jl::unique_socket::pipes();
    jl::mmsg_buffer<char> receiver(std::move(b), 3);

    CHECK(3 == jl::send(*sender, "foo"));
    CHECK(3 == jl::send(*sender, "bar"));

    auto msgs = receiver.recvmmsg();
    REQUIRE(1 == msgs.size());
    CHECK("foobar" == as_view(msgs[0]));
  }
}
