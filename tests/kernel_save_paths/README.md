# Direct Xbox save-device paths

Checks that Partition1 TDATA/UDATA roots and descendants agree with T:/U:,
that similarly named asset directories retain the general game mapping, and
that opening a raw partition still selects its image. Uses a temporary save
directory and synthetic data; no game files are needed.

Configure with the VS 2022 generator, build `kernel_save_paths_test` in
RelWithDebInfo, then run it or use CTest with that configuration.
