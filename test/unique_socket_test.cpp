#include <gtest/gtest.h>
#include <jl.h>

TEST(UniqueSocket, Pipes) {
  auto [in, out] = jl::unique_socket::pipes();

  EXPECT_EQ(3, jl::send(*out, std::string_view("foo")));

  std::string buffer = "xxxx";
  EXPECT_EQ("foo", jl::recv(*in, buffer));
}

static inline std::string_view as_view(std::span<char> data) {
  return {data.begin(), data.end()};
}

TEST(UniqueSocket, UDP) {
  auto receiver = jl::unique_socket::udp({"::", "4321"});
  auto sender = jl::unique_socket::udp();
  jl::connect(*sender, {"127.0.0.1", "4321"});

  EXPECT_EQ(3, jl::send(*sender, "foo"));
  EXPECT_EQ(3, jl::send(*sender, "bar"));
  std::string buffer = "xxxxxxx";
  EXPECT_EQ("foo", jl::recv(*receiver, buffer, MSG_DONTWAIT));
  EXPECT_EQ("bar", jl::recv(*receiver, buffer, MSG_DONTWAIT));
}

TEST(UniqueSocket, TCP) {
  auto server = jl::unique_socket::tcp({"::", "4321"});
  jl::listen(*server, 2);

  auto sender = jl::unique_socket::tcp();
  jl::connect(*sender, {"::1", "4321"});

  EXPECT_EQ(3, jl::send(*sender, "foo"));
  EXPECT_EQ(3, jl::send(*sender, "bar"));

  auto accepted = jl::accept(*server);
  ASSERT_TRUE(accepted.has_value());  // NOLINTNEXTLINE(*unchecked-optional*)
  auto& [receiver, addr] = *accepted;
  EXPECT_EQ("::1", addr.host);
  std::string buffer = "xxxxxxx";
  EXPECT_EQ("foobar", jl::recv(*receiver, buffer, MSG_DONTWAIT));
}

TEST(UniqueSocket, BothIPv4Andv6SocketsCanBeDefaultBound) {
  jl::bind(*jl::unique_socket(socket(AF_INET, SOCK_DGRAM, 0)));
  jl::bind(*jl::unique_socket(socket(AF_INET, SOCK_STREAM, 0)));
  jl::bind(*jl::unique_socket(socket(AF_INET6, SOCK_DGRAM, 0)));
  jl::bind(*jl::unique_socket(socket(AF_INET6, SOCK_STREAM, 0)));
}

TEST(UniqueSocket, RecvAndSendWorksWithVariousInputs) {
  auto [in, out] = jl::unique_socket::pipes();
  std::vector<char> char_vector = {'f', 'o', 'o'};
  std::string string = "bar";
  std::vector<int> int_vector = {1, 2, 3};

  EXPECT_EQ(3, jl::send(*out, std::span<char>(char_vector)));
  EXPECT_EQ(3, jl::send(*out, string));
  EXPECT_EQ(3, jl::send(*out, int_vector));

  auto foo = jl::recv(*in, char_vector);
  EXPECT_EQ("foo", std::string_view(foo.begin(), foo.end()));
  EXPECT_EQ("bar", jl::recv(*in, string));
  auto int123 = jl::recv(*in, std::span<int>(int_vector));
  EXPECT_EQ((std::vector<int>{1, 2, 3}), std::vector<int>(int123.begin(), int123.end()));
}

TEST(UniqueSocket, MMSGBufferDatagrams) {
  auto [a, b] = jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM);
  jl::mmsg_buffer<char> sender(std::move(a), 3);
  jl::mmsg_buffer<char> receiver(std::move(b), 3);

  sender.write(0, std::string_view("foo"));
  sender.write(1, std::string_view("bar"));
  EXPECT_EQ(2, sender.sendmmsg(0, 2));

  auto msgs = receiver.recvmmsg();
  ASSERT_EQ(2, msgs.size());
  EXPECT_EQ("foo", as_view(msgs[0]));
  EXPECT_EQ("bar", as_view(msgs[1]));
}

TEST(UniqueSocket, MMSGBufferStream) {
  auto [sender, b] = jl::unique_socket::pipes();
  jl::mmsg_buffer<char> receiver(std::move(b), 3);

  EXPECT_EQ(3, jl::send(*sender, "foo"));
  EXPECT_EQ(3, jl::send(*sender, "bar"));

  auto msgs = receiver.recvmmsg();
  ASSERT_EQ(1, msgs.size());
  EXPECT_EQ("foobar", as_view(msgs[0]));
}
