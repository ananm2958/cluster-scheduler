# cluster-scheduler

## Phase 1: Linux namespace launcher

`ns-launcher` starts one command in new user, PID, mount, UTS, network, and
IPC namespaces. It is a learning-sized process launcher, not a production
container runtime. In particular, it does not configure cgroups, seccomp,
capability dropping, or a network policy.

### Build

```sh
make
```

### Run

The command needs a **root filesystem directory** containing the executable
and its shared libraries. A small Alpine/minirootfs unpacked into
`./rootfs`, for example, is convenient.

```sh
./ns-launcher --root ./rootfs --hostname demo -- /bin/sh
```

Inside that shell, `echo $$` prints `1`, `hostname` prints `demo`, and `ip
link` (if included in the rootfs) shows no host interfaces. The new network
namespace initially has no configured network path; this launcher deliberately
does not connect it to the host.

Unprivileged user namespaces must be enabled by the host kernel. On systems
where they are disabled, the program reports the failed kernel operation.

## How it works

1. The launcher first calls `unshare(CLONE_NEWUSER)`, then maps the caller's
   current UID and GID to UID/GID 0 *inside* the new user namespace. This gives
   the child the namespace-local privilege needed to create mounts without
   giving it host-root identity.
2. It unshares mount, UTS, network, IPC, and PID namespaces. It makes mount
   propagation private so mounts made in the container cannot propagate back
   to the host (or vice versa).
3. PID namespaces apply to future children, so it forks once. That child is
   PID 1 in the new PID namespace and executes the requested program; the
   original process waits and forwards its exit status.
4. PID 1 bind-mounts the requested root directory, `chroot`s into it, and
   mounts a fresh `/proc`. The bind mount makes the selected directory a mount
   boundary before changing root. This is why the program asks for an explicit
   rootfs rather than pretending that a mount namespace alone hides the host.

### Namespace boundaries

| Namespace | Isolates | Does **not** isolate |
| --- | --- | --- |
| PID | Process-ID numbering and the visible process tree | CPU, memory, signals from host-root, or resource limits |
| Mount | Mount table and mount propagation | Filesystem contents unless a separate root/mounts are installed |
| UTS | Hostname and NIS domain name | Network identity or DNS configuration |
| Network | Interfaces, routes, firewall state, ports, sockets | Network access if a veth/route is deliberately added |
| IPC | System V IPC and POSIX message queues | Ordinary files, pipes inherited across exec, or shared memory passed by FD |
| User | UID/GID mappings and namespace-scoped capabilities | Host permissions beyond mapped identities; it is not a complete sandbox |

The launcher closes no inherited file descriptors and does not sanitize the
environment; a real runtime must handle both, reap orphaned children as PID 1,
and apply cgroups, seccomp, and a capability policy.
