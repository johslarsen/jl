#include <benchmark/benchmark.h>
#include <jl.h>
#include <poll.h>

#include <iostream>
#include <latch>
#include <thread>

static std::vector<int64_t> sizes{1, 128, 1024, 65500};
static std::vector<int64_t> bursts{128};

static void send_loop(const std::stop_token& token, std::atomic<bool>& finished, jl::unique_socket fd, size_t message_size) {
  std::vector<char> buffer(message_size);
  while (!token.stop_requested()) {
    try {
      fd.send(buffer);
    } catch (const std::system_error&) {
      // ignore intermittent ECONNREFUSED errors
    }
  }
  finished = true;
}

void drain(std::atomic<bool>& sender_finished, jl::unique_socket& fd, std::span<char> buffer) {
  while (!sender_finished) {
    (void)fd.recv(buffer, MSG_DONTWAIT).empty();
  }
}

static inline std::pair<jl::unique_socket, jl::unique_socket> connected_udp() {
  auto in = jl::unique_socket::udp({"::", "1234"});
  auto out = jl::unique_socket::udp();
  out.connect({"::", "1234"});
  return {std::move(in), std::move(out)};
}

static inline std::pair<jl::unique_socket, jl::unique_socket> connected_tcp() {
  auto server = jl::unique_socket::tcp({"::", "1234"});
  server.listen(1);
  auto client = jl::unique_socket::tcp();
  client.connect({"::", "1234"});
  auto [conn, _] = server.accept().value();  // NOLINT(*unchecked-optional*), crashing is fine
  return {std::move(conn), std::move(client)};
}

void BM_RecvIntoSameBuffer(benchmark::State& state, std::pair<jl::unique_socket, jl::unique_socket> sockets, auto sender_loop) {
  size_t message_size = state.range(0);
  auto [in, out] = std::move(sockets);
  std::atomic<bool> sender_finished = false;
  std::jthread sender(sender_loop, std::ref(sender_finished), std::move(out), message_size);

  std::vector<char> buffer(message_size);
  size_t packets = 0, bytes = 0;
  for (auto _ : state) {
    bytes += in.recv(buffer).size();
    packets += 1;
  }
  sender.request_stop();
  drain(sender_finished, in, buffer);

  state.counters["Bytes"] = benchmark::Counter(static_cast<double>(bytes), benchmark::Counter::kIsRate);
  state.counters["Packets"] = benchmark::Counter(static_cast<double>(packets), benchmark::Counter::kIsRate);
}
BENCHMARK_CAPTURE(BM_RecvIntoSameBuffer, DatagramPipe, jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM), send_loop)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoSameBuffer, UDP, connected_udp(), send_loop)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoSameBuffer, StreamPipe, jl::unique_socket::pipes(), send_loop)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoSameBuffer, TCP, connected_tcp(), send_loop)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});

void BM_RecvIntoCircularBuffer(benchmark::State& state, std::pair<jl::unique_socket, jl::unique_socket> sockets, auto sender_loop) {
  size_t message_size = state.range(0);
  auto [in, out] = std::move(sockets);
  std::atomic<bool> sender_finished = false;
  std::jthread sender(sender_loop, std::ref(sender_finished), std::move(out), message_size);

  jl::CircularBuffer<char, 1 << 20> buffer;
  size_t packets = 0, bytes = 0;
  for (auto _ : state) {
    auto received = in.recv(buffer.peek_back(message_size));
    packets += 1;
    bytes += received.size();

    buffer.commit_written(std::span<char>(received));
    buffer.commit_read(std::move(received));
  }
  sender.request_stop();
  drain(sender_finished, in, buffer.peek_back(message_size));

  state.counters["Bytes"] = benchmark::Counter(static_cast<double>(bytes), benchmark::Counter::kIsRate);
  state.counters["Packets"] = benchmark::Counter(static_cast<double>(packets), benchmark::Counter::kIsRate);
}
BENCHMARK_CAPTURE(BM_RecvIntoCircularBuffer, DatagramPipe, jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM), send_loop)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoCircularBuffer, UDP, connected_udp(), send_loop)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoCircularBuffer, StreamPipe, jl::unique_socket::pipes(), send_loop)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoCircularBuffer, TCP, connected_tcp(), send_loop)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});

void BM_RecvmmsgIntoSameBuffer(benchmark::State& state, std::pair<jl::unique_socket, jl::unique_socket> sockets, auto sender_loop) {
  size_t message_size = state.range(0);
  size_t messages_per_burst = state.range(1);
  auto [in, out] = std::move(sockets);
  std::atomic<bool> sender_finished = false;
  std::jthread sender(sender_loop, std::ref(sender_finished), std::move(out), message_size);

  std::vector<char> buffer(message_size);
  std::vector<std::span<char>> messages(messages_per_burst, buffer);
  jl::mmsg_socket<char> mmsg(std::move(in), messages);
  size_t packets = 0, bytes = 0;
  for (auto _ : state) {
    for (const auto& msg : mmsg.recvmmsg()) {
      bytes += msg.size();
      packets += 1;
    }
  }
  sender.request_stop();
  drain(sender_finished, mmsg.fd(), buffer);

  state.counters["Bytes"] = benchmark::Counter(static_cast<double>(bytes), benchmark::Counter::kIsRate);
  state.counters["Packets"] = benchmark::Counter(static_cast<double>(packets), benchmark::Counter::kIsRate);
}
BENCHMARK_CAPTURE(BM_RecvmmsgIntoSameBuffer, DatagramPipe, jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM), send_loop)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgIntoSameBuffer, UDP, connected_udp(), send_loop)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgIntoSameBuffer, StreamPipe, jl::unique_socket::pipes(), send_loop)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgIntoSameBuffer, TCP, connected_tcp(), send_loop)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});

void BM_RecvmmsgPerMessageBuffer(benchmark::State& state, std::pair<jl::unique_socket, jl::unique_socket> sockets, auto sender_loop) {
  size_t message_size = state.range(0);
  size_t messages_per_burst = state.range(1);
  auto [in, out] = std::move(sockets);
  std::atomic<bool> sender_finished = false;
  std::jthread sender(sender_loop, std::ref(sender_finished), std::move(out), message_size);

  jl::mmsg_buffer<char> mmsg(std::move(in), messages_per_burst, message_size);
  size_t packets = 0, bytes = 0;
  for (auto _ : state) {
    for (const auto& msg : mmsg.recvmmsg()) {
      bytes += msg.size();
      packets += 1;
    }
  }
  sender.request_stop();
  drain(sender_finished, mmsg.fd(), mmsg.buffer(0));

  state.counters["Bytes"] = benchmark::Counter(static_cast<double>(bytes), benchmark::Counter::kIsRate);
  state.counters["Packets"] = benchmark::Counter(static_cast<double>(packets), benchmark::Counter::kIsRate);
}
BENCHMARK_CAPTURE(BM_RecvmmsgPerMessageBuffer, DatagramPipe, jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM), send_loop)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgPerMessageBuffer, UDP, connected_udp(), send_loop)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgPerMessageBuffer, StreamPipe, jl::unique_socket::pipes(), send_loop)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgPerMessageBuffer, TCP, connected_tcp(), send_loop)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});

#if 1
template <size_t MessagesPerBurst>
static void sendmmsg_loop(const std::stop_token& token, std::atomic<bool>& finished, jl::unique_socket fd, size_t message_size) {
  std::vector<char> buffer(message_size);
  std::vector<std::span<char>> messages(MessagesPerBurst, buffer);
  jl::mmsg_socket<char> mmsg(std::move(fd), messages);
  while (!token.stop_requested()) {
    try {
      mmsg.sendmmsg();
    } catch (const std::system_error&) {
      // ignore intermittent ECONNREFUSED errors
    }
  }
  finished = true;
}
BENCHMARK_CAPTURE(BM_RecvIntoSameBuffer, DatagramPipeSendmmsg, jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM), sendmmsg_loop<256>)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoSameBuffer, UDPSendmmsg, connected_udp(), sendmmsg_loop<256>)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoSameBuffer, StreamPipeSendmmsg, jl::unique_socket::pipes(), sendmmsg_loop<256>)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoSameBuffer, TCPSendmmsg, connected_tcp(), sendmmsg_loop<256>)
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvmmsgIntoSameBuffer, DatagramPipeSendmmsg, jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM), sendmmsg_loop<256>)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgIntoSameBuffer, UDPSendmmsg, connected_udp(), sendmmsg_loop<256>)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgIntoSameBuffer, StreamPipeSendmmsg, jl::unique_socket::pipes(), sendmmsg_loop<256>)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgIntoSameBuffer, TCPSendmmsg, connected_tcp(), sendmmsg_loop<256>)
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
#endif

void BM_NonBlockingRecvOnEmptySocket(benchmark::State& state, int socket_type, int recv_flags) {
  auto [in, _] = jl::unique_socket::pipes(AF_UNIX, socket_type);
  std::vector<char> buffer(1024);
  for (auto _ : state) {
    size_t n = 0;
    benchmark::DoNotOptimize(n = in.recv(buffer, recv_flags).size());
  }
}
BENCHMARK_CAPTURE(BM_NonBlockingRecvOnEmptySocket, DatagramOnSocketCreation, SOCK_DGRAM | SOCK_NONBLOCK, 0);
BENCHMARK_CAPTURE(BM_NonBlockingRecvOnEmptySocket, DatagramOnRecvCalls, SOCK_DGRAM, MSG_DONTWAIT);
BENCHMARK_CAPTURE(BM_NonBlockingRecvOnEmptySocket, StreamOnSocketCreation, SOCK_STREAM | SOCK_NONBLOCK, 0);
BENCHMARK_CAPTURE(BM_NonBlockingRecvOnEmptySocket, StreamOnRecvCalls, SOCK_STREAM, MSG_DONTWAIT);

void BM_NonBlockingSendOnFullSocket(benchmark::State& state, int socket_type, int recv_flags) {
  auto [_, out] = jl::unique_socket::pipes(AF_UNIX, socket_type);
  std::vector<char> buffer(1024);
  while (send(out.fd(), buffer.data(), buffer.size(), recv_flags) >= 0)
    ;  // fill the socket
  for (auto _ : state) {
    size_t n = 0;
    benchmark::DoNotOptimize(n = out.send(buffer, recv_flags));
  }
}
BENCHMARK_CAPTURE(BM_NonBlockingSendOnFullSocket, DatagramOnSocketCreation, SOCK_DGRAM | SOCK_NONBLOCK, 0);
BENCHMARK_CAPTURE(BM_NonBlockingSendOnFullSocket, DatagramOnRecvCalls, SOCK_DGRAM, MSG_DONTWAIT);
BENCHMARK_CAPTURE(BM_NonBlockingSendOnFullSocket, StreamOnSocketCreation, SOCK_STREAM | SOCK_NONBLOCK, 0);
BENCHMARK_CAPTURE(BM_NonBlockingSendOnFullSocket, StreamOnRecvCalls, SOCK_STREAM, MSG_DONTWAIT);

BENCHMARK_MAIN();
