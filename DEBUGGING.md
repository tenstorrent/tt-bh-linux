## Gotchas and Debugging tips

Some problems that may arise, and tips for resolving them. Please send a pull
request with your own.

### Eye catcher mismatch

```
./console/tt-bh-linux
Press Ctrl-A x to exit.

L2CPU[8, 3] debug descriptor: 5b8000ef
L2CPU[8, 3] debug descriptor eye catcher mismatch
```

Ensure the version of tt-kmd, the Tenstorrent kernel module, is up to date.

## Magic was 0, not 5554043909264439382

The networking component of the host tool looks for 5554043909264439382 in the
x280's DRAM. When it cannot be found, errors will be interspersed with the
console output:

```
[  OK  ] Reached target sysinit.target - System Initialization.
[  OK  ] Listening on sshd-unix-local.socke…temd-ssh-generator, AF_UNIX Local).
agic was 0, not 5554043909264439382 trying again
                                                9msystemd-hostnamed.socket - Hostname Service Socket.
[  OK  ] Reached target sockets.target - Socket Units.
[  OK  ] Reached target basic.target - Basic System.
[  OK  ] Started systemd-networkd.service - Network Configuration.
Magic was 0, not 5554043909264439382 trying again
                                                 [  OK  ] Finished networking.service - Raise network interfaces.
Magic was 0, not 5554043909264439382 trying again
                                                 Magic was 0, not 5554043909264439382 trying again
                                                                                                  Magic was 0, not 5554043909264439382 trying again
```

Ensure you have the latest x280 kernel with the tt networking driver loaded,
and the host tool is up to date.
