#!/bin/bash

set -e

just build_all

# Call this over download_rootfs so it doesn't redownload the large rootfs if it's already there
just _need_rootfs
