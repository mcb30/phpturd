PHP turd interception library
=============================

This library was inspired by [SuiteCRM](https://suitecrm.com).
SuiteCRM's PHP code is written in the style of an enraged toddler
dribbling onto a keyboard, and fails to understand even basic concepts
of separating code from data.  Patching the code (along with all
possible third-party plugin modules) to fix this fundamental design
flaw is not feasible.

This library allows for incompetently written PHP code (such as
SuiteCRM) to be forcibly divided into two top-level "turd"
directories: a distribution tree (where permissions should be set as
read-only) and a writable scratch area.  Accesses to any child paths
within either top-level directory will be mapped as follows:

- if the child path exists within the distribution tree, then access
  will be to that path within the distribution tree

- if the child path does not exist within the distribution tree, then
  access will be to the path within the writable scratch area.

The library must be loaded via `LD_PRELOAD` using e.g.

```shell
LD_PRELOAD=/usr/lib64/libphpturd.so
```

This can be configured for a PHP runtime environment by creating a
`systemd` drop-in configuration file.  For example: to configure
`php-fpm`, create the file
`/usr/lib/systemd/system/php-fpm.service.d/phpturd.conf` containing:

```ini
[Service]
Environment=LD_PRELOAD=/usr/lib64/libphpturd.so
```

The pair of turd directories is passed via the environment variable
`PHPTURD` in the form `<distribution tree>:<writable scratch area>`,
where each directory is in the form of a canonicalised path (as
returned by `realpath(3)`).  For example:

```shell
PHPTURD=/usr/share/suitecrm:/var/lib/suitecrm
```

The directories should be at the same depth in the filesystem, to
allow relative symlinks pointing outside of the turd directories to
resolve correctly.

Yes, this is hideously ugly.  But it's elegance personified compared
to anything found in the [SuiteCRM commit
log](https://github.com/salesagility/SuiteCRM/commits/master).
