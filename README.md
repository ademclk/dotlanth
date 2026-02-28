# Dotlanth

[![CI](https://github.com/ademclk/dotlanth/actions/workflows/ci.yml/badge.svg)](https://github.com/ademclk/dotlanth/actions/workflows/ci.yml)
[![CD](https://github.com/ademclk/dotlanth/actions/workflows/cd.yml/badge.svg)](https://github.com/ademclk/dotlanth/actions/workflows/cd.yml)
[![License: GPLv3 + Commercial](https://img.shields.io/badge/license-GPLv3%20%2B%20Commercial-black)](./LICENSE)

Software should start with an idea, not a setup checklist.

Dotlanth turns one `dot` file into a running API.

No framework ceremony.
No boilerplate maze.
Just intent, executed.

## One File. One Runtime.

In Dotlanth, the API spec language is called **dot**.
A project starts with a single `dot` file that defines routes and behavior.

Dotlanth does the rest:
- **dotDSL** parses dot.
- **DotVM** executes it safely.
- **DotDB** records state and run history.

## Why Dotlanth Exists

Building software should feel like building software.
Not wiring, scaffolding, and repetitive glue code.

Dotlanth removes setup drag so teams can move from idea to endpoint fast.

## A 60-Second Flow (Target UX)

Create and run:

```bash
dot init hello-api
cd hello-api
dot run
```

Call it:

```bash
curl http://localhost:8080/hello
```

Inspect runs:

```bash
dot logs --last 20
```

Illustrative `dot` file:

```dot
dot 0.1

app "hello-api"

allow log
allow net.http.listen

server listen 8080

api "public"
  route GET "/hello"
    respond 200 "Hello from Dotlanth"
  end
end
```

## Status

Dotlanth is early, by design.

Current milestone focus:
- [x] Foundation (workspace, local gates, CI/CD)
- [ ] HTTP runtime
- [ ] State model
- [ ] Database connectors
- [ ] Security model
- [ ] Plugins
- [ ] Studio hooks

## Development

Run the full local gate (matches CI):

```bash
just check
```

If you don't have `just` installed, run the underlying commands directly:

```bash
cargo fmt --check
cargo clippy --workspace --all-targets --all-features -- -D warnings
cargo test --workspace
```

## Contributing

If runtime systems, compilers, databases, and developer tools excite you:

1. Open an issue with a clear problem statement.
2. Propose one focused improvement.
3. Ship one clean change.

## License

Dotlanth is dual licensed:
- GNU General Public License v3.0 or later (GPLv3+)
- Commercial License

See [LICENSE](./LICENSE), [LICENSE-GPLv3](./LICENSE-GPLv3), and [LICENSE-COMMERCIAL](./LICENSE-COMMERCIAL).
