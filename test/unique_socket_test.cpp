#include <gtest/gtest.h>
#include <jl.h>

TEST(UniqueSocket, Pipes) {
  auto [a, b] = jl::unique_socket::pipes();

  EXPECT_EQ(3, a.send(std::string_view("foo")));

  std::string buffer = "xxxx";
  EXPECT_EQ("foo", b.recv(buffer));
}

static inline std::string_view as_view(std::span<char> data) {
  return {data.begin(), data.end()};
}

TEST(UniqueSocket, UDP) {
  auto receiver = jl::unique_socket::udp({"::", "4321"});
  auto sender = jl::unique_socket::udp();
  sender.connect({"127.0.0.1", "4321"});

  EXPECT_EQ(3, sender.send("foo"));
  EXPECT_EQ(3, sender.send("bar"));
  std::string buffer = "xxxxxxx";
  EXPECT_EQ("foo", receiver.recv(buffer, MSG_DONTWAIT));
  EXPECT_EQ("bar", receiver.recv(buffer, MSG_DONTWAIT));
}

TEST(UniqueSocket, TCP) {
  auto server = jl::unique_socket::tcp({"::", "4321"});
  server.listen(2);

  auto sender = jl::unique_socket::tcp();
  sender.connect({"::1", "4321"});

  auto accepted = server.accept();
  ASSERT_TRUE(accepted.has_value());  // NOLINTNEXTLINE(*unchecked-optional*)
  auto& [receiver, addr] = *accepted;

  EXPECT_EQ(3, sender.send("foo"));
  EXPECT_EQ(3, sender.send("bar"));
  std::string buffer = "xxxxxxx";
  EXPECT_EQ("foobar", receiver.recv(buffer, MSG_DONTWAIT));
  EXPECT_EQ("::1", addr.host);
}

TEST(UniqueSocket, BothIPv4Andv6SocketsCanBeDefaultBound) {
  jl::unique_socket(socket(AF_INET, SOCK_DGRAM, 0)).bind();
  jl::unique_socket(socket(AF_INET, SOCK_STREAM, 0)).bind();
  jl::unique_socket(socket(AF_INET6, SOCK_DGRAM, 0)).bind();
  jl::unique_socket(socket(AF_INET6, SOCK_STREAM, 0)).bind();
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

  EXPECT_EQ(3, sender.send("foo"));
  EXPECT_EQ(3, sender.send("bar"));

  auto msgs = receiver.recvmmsg();
  ASSERT_EQ(1, msgs.size());
  EXPECT_EQ("foobar", as_view(msgs[0]));
}
