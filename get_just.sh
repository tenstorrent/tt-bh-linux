#!/bin/bash

# Check if just is already in the path
if command -v just >/dev/null 2>&1; then
  echo "just is already installed and available in your PATH."
  exit 0
fi

# Determine the system architecture.
arch=$(uname -m)

# Map the architecture to the corresponding URL suffix.
case "$arch" in
  "x86_64")
    url="https://github.com/casey/just/releases/download/1.40.0/just-1.40.0-x86_64-unknown-linux-musl.tar.gz"
    ;;
  "aarch64")
    url="https://github.com/casey/just/releases/download/1.40.0/just-1.40.0-aarch64-unknown-linux-musl.tar.gz"
    ;;
  *)
    echo "Unsupported architecture: $arch"
    exit 1
    ;;
esac

# Download the correct binary.
echo "Downloading just from: $url"
curl -sSL "$url" -o just.tar.gz

# Extract the archive.
echo "Extracting archive..."
tar -xzf just.tar.gz just completions/just.bash

# Make the binary executable and move it to /usr/local/bin (requires sudo).
echo "Installing just to /usr/local/bin..."
sudo mv just /usr/local/bin/

echo "Installing completion to /etc/bash_completion.d/..."
sudo mv completions/just.bash /etc/bash_completion.d/

# Clean up the archive.
echo "Cleaning up..."
rm just.tar.gz
rmdir completions

echo "Installation complete.  You can now run 'just' from your terminal."
