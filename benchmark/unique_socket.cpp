#include <benchmark/benchmark.h>
#include <jl.h>
#include <poll.h>

#include <iostream>
#include <latch>
#include <thread>

static std::vector<int64_t> sizes{1, 128, 1024};
static std::vector<int64_t> bursts{128};

static void send_loop(const std::stop_token& token, jl::unique_socket fd, size_t message_size) {
  std::vector<char> buffer(message_size);
  while (!token.stop_requested()) {
    try {
      fd.send(buffer);
    } catch (const std::system_error&) {
      // ignore intermittent ECONNREFUSED errors
    }
  }
}

void drain(jl::unique_socket& fd, std::span<char> buffer) {
  while (!fd.recv(buffer, MSG_DONTWAIT).empty())
    ;
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
  auto [conn, _] = server.accept().value(); // NOLINT(*unchecked-optional*), crashing is fine
  return {std::move(conn), std::move(client)};
}

void BM_RecvIntoSameBuffer(benchmark::State& state, std::pair<jl::unique_socket, jl::unique_socket> sockets) {
  size_t message_size = state.range(0);
  auto [in, out] = std::move(sockets);
  std::jthread sender(send_loop, std::move(out), message_size);

  std::vector<char> buffer(message_size);
  size_t packets = 0, bytes = 0;
  for (auto _ : state) {
    bytes += in.recv(buffer).size();
    packets += 1;
  }
  sender.request_stop();
  drain(in, buffer);

  state.counters["Bytes"] = benchmark::Counter(static_cast<double>(bytes), benchmark::Counter::kIsRate);
  state.counters["Packets"] = benchmark::Counter(static_cast<double>(packets), benchmark::Counter::kIsRate);
}
BENCHMARK_CAPTURE(BM_RecvIntoSameBuffer, UnixPipe, jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM))
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoSameBuffer, UDP, connected_udp())
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoSameBuffer, TCP, connected_tcp())
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});

void BM_RecvIntoCircularBuffer(benchmark::State& state, std::pair<jl::unique_socket, jl::unique_socket> sockets) {
  size_t message_size = state.range(0);
  auto [in, out] = std::move(sockets);
  std::jthread sender(send_loop, std::move(out), message_size);

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
  drain(in, buffer.peek_back(message_size));

  state.counters["Bytes"] = benchmark::Counter(static_cast<double>(bytes), benchmark::Counter::kIsRate);
  state.counters["Packets"] = benchmark::Counter(static_cast<double>(packets), benchmark::Counter::kIsRate);
}
BENCHMARK_CAPTURE(BM_RecvIntoCircularBuffer, UnixPipe, jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM))
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoCircularBuffer, UDP, connected_udp())
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});
BENCHMARK_CAPTURE(BM_RecvIntoCircularBuffer, TCP, connected_tcp())
    ->ArgName("MessageSize")
    ->ArgsProduct({sizes});

void BM_RecvmmsgIntoSameBuffer(benchmark::State& state, std::pair<jl::unique_socket, jl::unique_socket> sockets) {
  size_t message_size = state.range(0);
  size_t messages_per_burst = state.range(1);
  auto [in, out] = std::move(sockets);
  std::jthread sender(send_loop, std::move(out), message_size);

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
  drain(mmsg.fd(), buffer);

  state.counters["Bytes"] = benchmark::Counter(static_cast<double>(bytes), benchmark::Counter::kIsRate);
  state.counters["Packets"] = benchmark::Counter(static_cast<double>(packets), benchmark::Counter::kIsRate);
}
BENCHMARK_CAPTURE(BM_RecvmmsgIntoSameBuffer, UnixPipe, jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM))
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgIntoSameBuffer, UDP, connected_udp())
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgIntoSameBuffer, TCP, connected_tcp())
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});

void BM_RecvmmsgPerMessageBuffer(benchmark::State& state, std::pair<jl::unique_socket, jl::unique_socket> sockets) {
  size_t message_size = state.range(0);
  size_t messages_per_burst = state.range(1);
  auto [in, out] = std::move(sockets);
  std::jthread sender(send_loop, std::move(out), message_size);

  jl::mmsg_buffer<char> mmsg(std::move(in), messages_per_burst, message_size);
  size_t packets = 0, bytes = 0;
  for (auto _ : state) {
    for (const auto& msg : mmsg.recvmmsg()) {
      bytes += msg.size();
      packets += 1;
    }
  }
  sender.request_stop();
  drain(mmsg.fd(), mmsg.buffer(0));

  state.counters["Bytes"] = benchmark::Counter(static_cast<double>(bytes), benchmark::Counter::kIsRate);
  state.counters["Packets"] = benchmark::Counter(static_cast<double>(packets), benchmark::Counter::kIsRate);
}
BENCHMARK_CAPTURE(BM_RecvmmsgPerMessageBuffer, UnixPipe, jl::unique_socket::pipes(AF_UNIX, SOCK_DGRAM))
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgPerMessageBuffer, UDP, connected_udp())
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});
BENCHMARK_CAPTURE(BM_RecvmmsgPerMessageBuffer, TCP, connected_tcp())
    ->ArgNames({"MessageSize", "MessagesPerBurst"})
    ->ArgsProduct({sizes, bursts});

BENCHMARK_MAIN();
