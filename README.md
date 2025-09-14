# JBOD Project

This projects implements a networked client-server system using C network programming. 
Simulates a JBOD storage system and provides both server and client-side logic.

## Features

- **Networked server** for simulating JBOD operations (`jbod_server`)
- **Client-side logic** for interacting with the server
- **Caching** for efficient data access (`cache.c`, `cache.h`)
- **Utilities** for network communication (`net.c`, `net.h`)
- **Testing** framework (`tester.c`, `tester.h`)
- **Trace files** for test automation (see the `traces/` directory)

## File Overview

- `jbod_server`: Main server binary for JBOD operations
- `mdadm.c`, `mdadm.h`: Management code for sending/receiving commands
- `cache.c`, `cache.h`: Implements a cache for data operations
- `net.c`, `net.h`: Handles network communications
- `tester.c`, `tester.h`: Automated test runner and related helpers
- `util.c`, `util.h`: Utility functions
- `Makefile`: Build instructions
- `traces/`: Contains trace files for testing scenarios

## Building

To build the project, ensure you have a C compiler (e.g., `gcc`) installed. Run:

```bash
make
```

This will compile the binaries as specified in the Makefile.

## Usage

### Starting the Server

After building, start the server:

```bash
./jbod_server
```

### Running the Client

The client-side logic is implemented in `mdadm.c`. To run client-side interaction tests, use:

```bash
make test
```

followed by:

```bash
./tester
```
