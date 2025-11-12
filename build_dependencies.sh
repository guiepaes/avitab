#!/bin/bash

OUTDIR="$(pwd)/build-third/"
mkdir -p "$OUTDIR"
mkdir -p "$OUTDIR/lib"
mkdir -p "$OUTDIR/include"

cd lib || exit 1

# Newer CMake releases (4.1+) require projects that still target legacy
# compatibility policies to acknowledge that they expect behavior from at least
# CMake 3.5.  We pass the hint explicitly so the bundled third-party projects
# continue to configure without forcing users to edit their copies manually.
CMAKE_POLICY_FLAGS=(-DCMAKE_POLICY_VERSION_MINIMUM=3.5)

if ! command -v cmake >/dev/null 2>&1; then
    uname_s=$(uname -s 2>/dev/null || echo "")

    case "$uname_s" in
        MINGW*|MSYS*|CYGWIN*)
            for candidate in \
                /mingw64/bin/cmake \
                /mingw64/bin/cmake.exe \
                /c/msys64/mingw64/bin/cmake.exe \
                /c/Program\ Files/CMake/bin/cmake.exe \
                /c/Program\ Files\ \(x86\)/CMake/bin/cmake.exe
            do
                if [ -x "$candidate" ]; then
                    export PATH="$(dirname "$candidate"):$PATH"
                    break
                fi
            done
        ;;
    esac

    if ! command -v cmake >/dev/null 2>&1; then
        cat <<'EOF'
cmake was not found in your PATH.

If you are on Windows, abra o terminal "MSYS2 MinGW 64-bit" e execute
pacman -S mingw-w64-x86_64-cmake para instalar o pacote antes de continuar.
Caso já tenha instalado, execute o script a partir desse terminal ou adicione
"C:\msys64\mingw64\bin" ao PATH antes de continuar.

On Linux or macOS, install cmake using your package manager (for example,
apt install cmake ou brew install cmake).

Once cmake is available, run build_dependencies.sh again.
EOF
        exit 1
    fi
fi

echo "Building sqlite3..."
if [ ! -f "$OUTDIR/include/sqlite3/sqlite3.hpp" ]; then
    mkdir -p "$OUTDIR/include/sqlite3"
    cp sqlite3/sqlite3.h "$OUTDIR/include/sqlite3"
fi

if [ ! -f "$OUTDIR/include/sqlite3/sqlite3.h" ]; then echo "Failed"; exit 1; fi

echo "Building json..."
if [ ! -f "$OUTDIR/include/nlohmann/json.hpp" ]; then
    cp -a json/include/* "$OUTDIR/include/"
fi

if [ ! -f "$OUTDIR/include/nlohmann/json.hpp" ]; then echo "Failed"; exit 1; fi

echo "Building gulrak/filesystem..."
if [ ! -f "$OUTDIR/include/ghc/filesystem.hpp" ]; then
    mkdir -p "$OUTDIR/include/ghc"
    cp filesystem/include/ghc/*.hpp "$OUTDIR/include/ghc/"
fi
if [ ! -f "$OUTDIR/include/ghc/filesystem.hpp" ]; then echo "Failed"; exit 1; fi

echo "Building stb..."
if [ ! -f "$OUTDIR/include/stb/stb_image.h" ]; then
    mkdir -p "$OUTDIR/include/stb"
    cp stb/*.h "$OUTDIR/include/stb/"
fi
if [ ! -f "$OUTDIR/include/stb/stb_image.h" ]; then echo "Failed"; exit 1; fi

echo "Building XPLM..."
if [ ! -f "$OUTDIR/include/XPLM/XPLMPlugin.h" ]; then
    cp -a XSDK/CHeaders/XPLM "$OUTDIR/include"
    cp XSDK/Libraries/Win/* "$OUTDIR/lib"
    cp -a XSDK/Libraries/Mac/* "$OUTDIR/lib"
fi
if [ ! -f "$OUTDIR/include/XPLM/XPLMPlugin.h" ]; then echo "Failed"; exit 1; fi

echo "Building detex..."
if [ ! -f "$OUTDIR/lib/libdetex.a" ]; then
    cd detex || exit 1
    patch -p1 -s -N < ../patches/detex.patch
    OPTCFLAGS=-fPIC make library
    make HEADER_FILE_INSTALL_DIR="$OUTDIR/include" STATIC_LIB_DIR="$OUTDIR/lib" install
    cd ..
fi
if [ ! -f "$OUTDIR/lib/libdetex.a" ]; then echo "Failed"; exit 1; fi

echo "Building mupdf..."
if [ ! -f "$OUTDIR/lib/libmupdf-third.a" ]; then
    cd mupdf || exit 1
    patch -p1 -s -N < ../patches/mupdf.patch
    XCFLAGS=-fPIC make HAVE_X11=no HAVE_GLUT=no prefix="$OUTDIR" -j10 install
    cd thirdparty/libjpeg || exit 1
    ./configure
    cd ../../../
fi
if [ ! -f "$OUTDIR/lib/libmupdf-third.a" ]; then echo "Failed"; exit 1; fi

echo "Building mbedtls..."
if [ ! -f "$OUTDIR/lib/libmbedtls.a" ]; then
    cd mbedtls || exit 1
    mkdir -p build
    cd build || exit 1
    cmake -G "Unix Makefiles" "${CMAKE_POLICY_FLAGS[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-fPIC -DCMAKE_INSTALL_PREFIX="" -DCMAKE_HOST_UNIX=ON ..
    make -j10 DESTDIR="$OUTDIR" install
    cd ../../
fi
if [ ! -f "$OUTDIR/lib/libmbedtls.a" ]; then echo "Failed"; exit 1; fi

echo "Building curl..."
if [ ! -f "$OUTDIR/lib/libcurl.a" ]; then
    cd curl || exit 1
    git clean -f -d -x
    autoreconf -fi

    CURL_CONFIGURE_FLAGS=(
        --prefix="$OUTDIR"
        --with-mbedtls="$OUTDIR"
        --with-zlib="`pwd`/../mupdf/thirdparty/zlib/"
        --without-openssl
        --without-nghttp2
        --without-libpsl
        --without-brotli
        --without-winidn
        --without-libidn2
        --without-zstd
        --disable-shared
        --disable-crypto-auth
        --disable-ftp
        --disable-ldap
        --disable-telnet
        --disable-gopher
        --disable-dict
        --disable-imap
        --disable-pop3
        --disable-rtsp
        --disable-smtp
        --disable-tftp
    )

    uname_s=$(uname -s 2>/dev/null || echo "")
    uname_m=$(uname -m 2>/dev/null || echo "")
    case "$uname_s" in
        MINGW*|MSYS*|CYGWIN*)
            case "$uname_m" in
                x86_64|amd64)
                    host_triplet="x86_64-w64-mingw32"
                    ;;
                i?86)
                    host_triplet="i686-w64-mingw32"
                    ;;
                *)
                    host_triplet=""
                    ;;
            esac

            if [ -n "$host_triplet" ]; then
                CURL_CONFIGURE_FLAGS+=(--host=$host_triplet)
            fi

            # Some Windows shells fail to propagate the winsock detection
            # results during configure, which later makes the build abort
            # with "no non-blocking method was found".  Hint the expected
            # headers/functions so libcurl enables the Windows non-blocking
            # code paths even when detection is flaky.
            export CPPFLAGS="$CPPFLAGS -DHAVE_WINSOCK2_H -DHAVE_WINSOCK_H -DHAVE_IOCTLSOCKET"
        ;;
    esac

    ./configure "${CURL_CONFIGURE_FLAGS[@]}"
    CFLAGS=-fPIC make -j10 install
    cd ..
fi
if [ ! -f "$OUTDIR/lib/libcurl.a" ]; then echo "Failed"; exit 1; fi

echo "Building libtiff..."
if [ ! -f "$OUTDIR/lib/libtiff.a" ]; then
    cd libtiff || exit 1
    ./autogen.sh
    CFLAGS=-fPIC ./configure --prefix="$OUTDIR" --build=x86_64 --disable-jbig --disable-lzma --without-x
    make -j10 install
    cd ..
fi
if [ ! -f "$OUTDIR/lib/libtiff.a" ]; then echo "Failed"; exit 1; fi

echo "Building libproj..."
if [ ! -f "$OUTDIR/lib/libproj_5_2.a" ] || [ ! -f "$OUTDIR/lib/libproj.a" ]; then
    cd proj || exit 1
    mkdir -p build
    cd build || exit 1
    cmake -G "Unix Makefiles" "${CMAKE_POLICY_FLAGS[@]}" -DCMAKE_BUILD_TYPE=Release -DPROJ_TESTS=OFF -DBUILD_LIBPROJ_SHARED=OFF -DCMAKE_C_FLAGS=-fPIC -DCMAKE_INSTALL_PREFIX="$OUTDIR" -DLIBDIR="$OUTDIR/lib" -DINCLUDEDIR="$OUTDIR/include" -DBINDIR="$OUTDIR/bin" ..
    cd src || exit 1
    make -j10 install
    cp "$OUTDIR/lib/libproj.a" "$OUTDIR/lib/libproj_5_2.a"
    cp "$OUTDIR/lib/libproj_5_2.a" "$OUTDIR/lib/libproj.a"
    cd ../../..
fi
if [ ! -f "$OUTDIR/lib/libproj_5_2.a" ]; then echo "Failed"; exit 1; fi

echo "Building libgeotiff..."
if [ ! -f "$OUTDIR/lib/libgeotiff.a" ]; then
    cd libgeotiff/libgeotiff || exit 1
    mkdir -p build
    cd build || exit 1
    cmake -G "Unix Makefiles" "${CMAKE_POLICY_FLAGS[@]}" -DCMAKE_INSTALL_PREFIX="$OUTDIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-fPIC -DCMAKE_PREFIX_PATH="`pwd`/../../../" ..
    make -j10 install
    cp lib/libxtiff.a "$OUTDIR/lib"
    cd ../../..
fi
if [ ! -f "$OUTDIR/lib/libgeotiff.a" ]; then echo "Failed"; exit 1; fi

echo "Building QuickJS..."
if [ ! -f "$OUTDIR/lib/libquickjs.a" ]; then
    cd QuickJS || exit 1
    if [ -f VERSION-QuickJS ]; then mv VERSION-QuickJS VERSION ; fi
    make CC="gcc -fPIC" -j10 libquickjs.a
    if [ -f VERSION ]; then mv VERSION VERSION-QuickJS ; fi
    cp libquickjs.a "$OUTDIR/lib"
    cd ..
fi
if [ ! -f "$OUTDIR/lib/libquickjs.a" ]; then echo "Failed"; exit 1; fi

cd ..
