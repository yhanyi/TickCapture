# TickCapture

Attempt at creating a high-throughput (simulated) market data capture system to efficiently process and store real-time financial market data. The target was to handle up to 50,000 messages per second but only tested up to 10,000 with reliable capture and storage capabilities.

### Overview

TickCapture aims to simulate a complete pipeline for:
- Generating simulated market data
- Capturing data via multicast UDP
- Processing messages in real-time 
- Storing data efficiently
- Verifying data integrity

This project was designed to simulate low-latency environments where reliability and performance are critical.

## Features

- High-throughput message processing (50,000+ msgs/sec)
- Zero-copy message handling
- Lock-free ring buffer implementation
- Real-time data validation
- Message integrity verification
- Per-symbol file storage
- Performance benchmarking tools
- Configurable rates and durations
- Detailed statistics and monitoring

### Prerequisites

- CMake 3.15 or higher
- C++20 compatible compiler
- Boost libraries
- Intel TBB
- fmt library

### System Requirements

Before running at high message rates, configure system limits (not tested, run at your own discretion):

For Linux:
```bash
sudo sysctl -w net.core.rmem_max=26214400    # 25MB for receive buffer
sudo sysctl -w net.core.wmem_max=26214400    # 25MB for send buffer
sudo sysctl -w net.core.rmem_default=26214400
sudo sysctl -w net.core.wmem_default=26214400
```

For macOS:
```bash
sudo sysctl -w kern.ipc.maxsockbuf=26214400
```

## Building

```bash
# Clone the repository
git clone https://github.com/yhanyi/TickCapture.git
cd TickCapture

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make
```

## Running Benchmarks

The benchmark tool supports various rates and durations:

```bash
# Basic test (1000 msgs/sec for 10 seconds)
./tick_capture_benchmark --rate 1000 --duration 10

# Higher rate test
./tick_capture_benchmark --rate 5000 --duration 30

# Custom output directory
./tick_capture_benchmark --rate 1000 --output-dir /path/to/output

# Enable latency measurements
./tick_capture_benchmark --rate 1000 --latency
```

### Performance

Current performance metrics on a standard development machine (view the logs folder):
- 1,000 msgs/sec: Perfect capture, no drops
- 5,000 msgs/sec: 100% reliability
- 10,000 msgs/sec: ~99.99% capture rate

Testing was not conducted beyond 20,000 msgs/sec to avoid using `sudo` for system modifications.

## Configuration

Key configuration parameters:
```cpp
struct CaptureConfig {
    std::string multicast_addr = "239.255.0.1";
    uint16_t port = 12345;
    size_t ring_buffer_size = 65536;        // Ring buffer entries
    size_t udp_buffer_size = 65536;         // UDP receive buffer
    size_t socket_buffer_size = 33554432;   // Socket buffer (32MB)
    std::string output_dir;
    bool enable_timestamps = false;
};
```

Thank you for checking out this project! :)
