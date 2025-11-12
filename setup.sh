#!/bin/bash

# Constants
BUILD_FOLDER="$(pwd)"/build
CMAKE_POLICY_FLAGS=(-DCMAKE_POLICY_VERSION_MINIMUM=3.5)

aptInstall() {
  sudo apt install -y "$1"
}

pacmanInstall() {
  pacman -S --needed --noconfirm "$1"
}

brewInstall() {
  brew install "$1"
}

copyResources() {
 # Move files from res/ to build/
  echo "Moving files..."
  if [ ! -f "$BUILD_FOLDER"/config.json ]; then
    cp -a "$(pwd)"/res/* "$BUILD_FOLDER"
    cp -a "$(pwd)"/res/* "$BUILD_FOLDER"
    cd ..
  fi
  if [ ! -f "$BUILD_FOLDER"/config.json ]; then
    echo "Failed"
    exit
  fi

  cd "$BUILD_FOLDER" || exit
}

standalone() {
  echo "Making AviTab-standalone..."
  copyResources
  make AviTab-standalone
}

plugin() {
  echo "Making avitab_plugin..."
  copyResources
  make avitab_plugin
}

# OS Detection
detect_platform() {
  if [ -n "$OSTYPE" ]; then
    printf '%s' "$OSTYPE"
    return
  fi

  uname -s 2>/dev/null | tr '[:upper:]' '[:lower:]'
}

PLATFORM=$(detect_platform)

case "$PLATFORM" in
linux*|gnu/linux*)
  echo "Linux detected..."
  aptInstall cmake
  aptInstall make
  aptInstall autoconf
  aptInstall automake
  aptInstall libtool
  aptInstall libglfw3
  aptInstall libglfw3-dev
  aptInstall uuid-dev
  ;;
msys*|mingw*)
  echo "Windows detected..."
  echo "Have you installed MSYS2?"
  printf "1. Yes\n2. No\n"

  read -r answer

  if [ "$answer" == "1" ]; then
        if ! command -v pacman >/dev/null 2>&1; then
      cat <<'EOF'
pacman was not found in your PATH. Please launch the "MSYS2 MinGW 64-bit"
terminal that ships with MSYS2 and run this script from there so that the
toolchain and package manager are available.

O comando pacman não foi encontrado na sua variável PATH. Abra o terminal
"MSYS2 MinGW 64-bit" (disponibilizado pelo MSYS2) e execute este script a
partir dele para garantir que as ferramentas de compilação e o gerenciador de
pacotes estejam disponíveis.
EOF
      exit 1
    fi
    pacmanInstall mingw-w64-x86_64-toolchain
    pacmanInstall mingw64/mingw-w64-x86_64-cmake
    pacmanInstall msys/git
    pacmanInstall msys/patch
    pacmanInstall msys/make
    pacmanInstall msys/autoconf
    pacmanInstall msys/automake
    pacmanInstall msys/libtool
    pacmanInstall mingw64/mingw-w64-x86_64-glfw
  elif [ "$answer" == "2" ]; then
    echo "Please download it from: https://www.msys2.org/ and install it, then run this script again."
    exit
  else
    echo "Exiting..."
    exit
  fi
  ;;
darwin*)
  echo "macOS detected..."
  echo "Have you installed brew?"
  printf "1. Yes\n2. No\n"

  read -r answer

  if [ "$answer" == "1" ]; then
    brewInstall cmake
    brewInstall make
    brewInstall autoconf
    brewInstall automake
    brewInstall libtool
    brewInstall glfw
    brewInstall pkgconfig
  elif [ "$answer" == "2" ]; then
    echo "Please download it from: https://brew.sh/ and install it, then run this script again."
    exit
  else
    echo "Exiting..."
    exit
  fi
  ;;
*)
 echo "Unknown system, exiting..."
  exit
  ;;
esac

# Build third party dependencies in the build-third directory
echo "Running build_dependencies..."
./build_dependencies.sh

# Setup build folder properly
echo "Running CMake..."
cmake -G 'Unix Makefiles' -B "$BUILD_FOLDER" "${CMAKE_POLICY_FLAGS[@]}"

echo "Select next step..."
printf "1. Make AviTab-Standalone\n2. Make avitab-plugin\n3. Nothing\n"

read -r selection

case "$selection" in
1)
  standalone
  ;;
2)
  plugin
  ;;
*)
  echo "Exiting..."
  ;;
esac
