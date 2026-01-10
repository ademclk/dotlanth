# Dotlanth

AI-Powered Automation Platform

## Overview

Dotlanth is a modular automation platform designed for building intelligent workflows. It provides a unified runtime for executing tasks, managing state, and orchestrating autonomous agents—suitable for individual developers, teams, and organizations of any size.

### Core Components

- **DotVM** - High-performance virtual machine for executing automation workflows with sandboxed execution and resource management
- **DotDB** - Integrated data layer for state persistence, caching, and cross-workflow data sharing
- **DotAgent** - Autonomous agent framework with configurable decision-making, human-in-the-loop oversight, and multi-agent coordination

### Key Features

- **Workflow Orchestration** - Define, schedule, and monitor complex multi-step automation pipelines
- **Plugin Architecture** - Extend functionality with custom plugins and integrations
- **Event-Driven Execution** - Trigger workflows based on events, schedules, or external signals
- **Observability** - Built-in logging, metrics, and tracing for debugging and monitoring

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Running

```bash
./dotlanth
```

## Project Structure

```
dotlanth/
├── src/           # Source files
├── include/       # Header files
├── CMakeLists.txt # Build configuration
└── README.md
```

## License

Proprietary
