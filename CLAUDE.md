# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This repository contains example applications for the Axis Camera Application Platform (ACAP) Native SDK. These examples demonstrate how to build plug-in style applications for Axis devices.

## Build Commands

All examples use Docker-based builds. The standard build process for any example:

```sh
# Build for armv7hf (default architecture)
docker build --tag <example-name>:latest <example-directory>

# Build for aarch64
docker build --build-arg ARCH=aarch64 --tag <example-name>:latest <example-directory>

# Extract build artifacts from container
docker cp $(docker create <example-name>:latest):/opt/app ./build
```

Build artifacts (.eap files) are created in `/opt/app/` inside the container.

## Linting

Run all linters before submitting changes:

```sh
./run-linters --all              # Run all linters
./run-linters --super-linter     # Super-linter only (Dockerfile, Markdown, YAML, JSON, C/C++, shell)
./run-linters --custom-linter    # Custom linters only
```

Autoformat C/C++ files using the super-linter container:
```sh
docker run --rm -u $(id -u):$(id -g) -e USER -v $PWD:/tmp/lint -w /tmp/lint --entrypoint /bin/bash -it ghcr.io/super-linter/super-linter:slim-v8
clang-format -i <path/to/file>
```

## Architecture

### Example Structure
Each example follows this standard layout:
```
example-name/
├── app/
│   ├── *.c or *.cpp        # Source code
│   ├── LICENSE             # Apache 2.0
│   ├── Makefile            # Build rules (optional for arch=all)
│   └── manifest.json       # ACAP package configuration
├── Dockerfile              # Build container definition
└── README.md               # Example documentation
```

### Dockerfile Pattern
All Dockerfiles use the ACAP Native SDK base image:
```dockerfile
ARG ARCH=armv7hf
ARG VERSION=12.7.0
ARG UBUNTU_VERSION=24.04
FROM axisecp/acap-native-sdk:${VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION}

COPY ./app /opt/app/
WORKDIR /opt/app
RUN . /opt/axis/acapsdk/environment-setup* && acap-build ./
```

### Makefile Pattern
C/C++ examples use strict warning flags with `-Werror`:
```makefile
PROG1 = $(shell jq -r '.acapPackageConf.setup.appName' manifest.json)
CFLAGS += -Wall -Wextra -Wformat=2 -Wpointer-arith -Wbad-function-cast \
          -Wstrict-prototypes -Wmissing-prototypes -Winline \
          -Wdisabled-optimization -Wfloat-equal -W -Werror
```

## Code Style Conventions

### C Code
- Prefer C over C++ for clarity and brevity
- Use `panic()` function for error handling (logs to syslog, exits with 1)
- Comments explain "why", code shows "how" - use `//` style only
- Function names should explain what is done; functions should do one thing
- Place main function last to avoid forward declarations
- Use standard libc by default; glib when it saves complexity

### Formatting
- C/C++: clang-format (version 17, compatible with 15-17), 4-space indent, 100-char column limit
- Markdown/YAML: 2-space indent
- JSON: 4-space indent
- Makefiles: tab indentation

## CI/CD

GitHub Actions workflows in `.github/workflows/`:
- `linter.yml` - Runs all linters on PR/push
- Individual build workflows per example (triggered on path changes)

Build workflows test both `armv7hf` and `aarch64` architectures.

## Key Guidelines

- Use general names without versions (say "ACAP Native SDK" not "ACAP Native SDK 12")
- Don't add proxy variables in Dockerfiles
- All source files need Apache 2.0 copyright header
- Follow guiding examples: `axparameter/` and `vapix/`
