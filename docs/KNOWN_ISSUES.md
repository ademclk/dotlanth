# Known Issues — v26.1.0-alpha

This document tracks known limitations and incomplete implementations. Issues are listed in the release where they were identified.

---

## v26.1.0-alpha

### Package Management

| Issue | Impact | Workaround |
|-------|--------|------------|
| Archive extraction not implemented | Cannot install packages from `.tar.gz` | Install from directories only |

*Reference: `src/dotvm/pkg/package_cache.cpp:57`*

### Replication (Experimental)

| Issue | Impact | Workaround |
|-------|--------|------------|
| RaftLog not persisted | State lost on restart | Use for testing only |
| Checksum verification incomplete | Data integrity not validated | Verify manually |
| MPT root verification pending | Merkle proofs not validated | Trust source |
| Local node ID assignment incomplete | Node identification unreliable | Single-node use only |

*References: `src/dotvm/core/state/replication/raft_log.cpp:222`, `delta_subscriber.cpp:76,150,169,181`*

### Debugging

| Issue | Impact | Workaround |
|-------|--------|------------|
| Call stack incomplete | Debugger shows partial stack | Use logging |

*Reference: `src/dotvm/debugger/debug_client.cpp:450`*

### Test Coverage

| Issue | Impact | Workaround |
|-------|--------|------------|
| Delta subscriber checksum test incomplete | Test validation limited | Manual verification |

*Reference: `tests/dotvm/core/state/replication/delta_subscriber_test.cpp:132`*

---

## Reporting New Issues

If you encounter problems not listed here, please [open an issue](https://github.com/ademclk/dotlanth/issues) with:

1. DotVM version (`dotvm --version`)
2. Platform and compiler version
3. Minimal reproduction steps
4. Expected vs actual behavior
