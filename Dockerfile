FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential cmake git \
    libocct-data-exchange-dev \
    libocct-draw-dev \
    libocct-foundation-dev \
    libocct-modeling-algorithms-dev \
    libocct-modeling-data-dev \
    libocct-visualization-dev \
    libgl-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxi-dev libxkbcommon-dev \
    libwayland-dev pkg-config \
    libcurl4-openssl-dev \
    zlib1g-dev \
    file patchelf wget fuse libfuse2 \
    imagemagick \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Build the project
RUN mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
    && make -j$(nproc)

# Download appimagetool
RUN ARCH=$(uname -m) \
    && wget -q "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage" \
        -O /usr/local/bin/appimagetool \
    && chmod +x /usr/local/bin/appimagetool

# ─── Create AppDir structure ────────────────────────────────────────────────

RUN mkdir -p /AppDir/usr/bin /AppDir/usr/lib \
    /AppDir/usr/share/icons/hicolor/256x256/apps \
    /AppDir/usr/share/icons/hicolor/512x512/apps

# Copy binary
RUN cp /src/build/materializr /AppDir/usr/bin/materializr

# Copy OCCT + TBB + Freetype shared libs (follow symlinks, any arch)
RUN find /usr/lib -name "libTK*.so*" -o -name "libtbb*.so*" -o -name "libfreetype.so*" \
    | while read lib; do cp -L "$lib" /AppDir/usr/lib/ 2>/dev/null || true; done

# Set RPATH
RUN patchelf --set-rpath '$ORIGIN/../lib' /AppDir/usr/bin/materializr || true

# Create .desktop file. StartupWMClass must match the WM_CLASS / Wayland
# app-id the running window reports (set in Window.cpp via the GLFW hints)
# so taskbar extensions like Dash-to-Panel can tie the window to its icon.
RUN printf '[Desktop Entry]\nName=Materializr\nExec=materializr\nIcon=materializr\nType=Application\nCategories=Graphics;3DGraphics;Engineering;\nComment=Open-source parametric 3D CAD\nStartupWMClass=Materializr\n' \
    > /AppDir/materializr.desktop

# Use the project's icon.png if present at the repo root, resized to the
# canonical 256x256 + 512x512 hicolor sizes so desktop environments pick
# them up cleanly. Falls back to a tiny generated SVG placeholder during
# early bring-up if no icon.png is committed yet. `-background none` keeps
# transparency intact; `-resize` preserves aspect and pads with transparent
# pixels so non-square sources land centred in a square frame.
RUN if [ -f /src/icon.png ]; then \
        convert /src/icon.png -background none -resize 256x256 \
            -gravity center -extent 256x256 /AppDir/materializr.png && \
        cp /AppDir/materializr.png \
            /AppDir/usr/share/icons/hicolor/256x256/apps/materializr.png && \
        convert /src/icon.png -background none -resize 512x512 \
            -gravity center -extent 512x512 \
            /AppDir/usr/share/icons/hicolor/512x512/apps/materializr.png ; \
    else \
        printf '<?xml version="1.0"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256">\n<rect width="256" height="256" rx="32" fill="#2a2a3a"/>\n<text x="128" y="160" font-size="120" font-family="sans-serif" font-weight="bold" fill="#4a9eff" text-anchor="middle">C</text>\n</svg>\n' \
            > /AppDir/materializr.svg && \
        cp /AppDir/materializr.svg /AppDir/usr/share/icons/hicolor/256x256/apps/materializr.svg ; \
    fi

# Create AppRun script
RUN printf '#!/bin/bash\nHERE="$(dirname "$(readlink -f "$0")")"\nexport LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"\nexec "$HERE/usr/bin/materializr" "$@"\n' \
    > /AppDir/AppRun \
    && chmod +x /AppDir/AppRun

# Build the AppImage (--appimage-extract-and-run avoids FUSE requirement inside Docker)
RUN mkdir -p /output \
    && ARCH=$(uname -m) \
    && appimagetool --appimage-extract-and-run /AppDir /output/Materializr-${ARCH}.AppImage

# ─── Export stage ────────────────────────────────────────────────────────────

FROM scratch AS export
COPY --from=builder /output/*.AppImage /
