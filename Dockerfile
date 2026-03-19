# --- Stage 1: Build ---
FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libgomp1 \
    autoconf \
    automake \
    libtool \
    pkg-config \
    ca-certificates \
    git \
    meson \
    ninja-build \
    libfcgi-dev \
    libjpeg-turbo8-dev \
    libmemcached-dev \
    libtiff-dev \
    libpng-dev \
    zlib1g-dev \
    libglib2.0-dev \
    libcairo2-dev \
    libgdk-pixbuf-2.0-dev \
    libsqlite3-dev \
    libxml2-dev \
    libopenjp2-7-dev \
    && rm -rf /var/lib/apt/lists/*

# Build OpenSlide 4.0.0 from source (first version with DICOM support via libdicom subproject)
# -Dlibdir=lib forces install to /usr/local/lib (not arch-specific subdir) for easy copying
RUN set -ex \
    && git clone https://github.com/openslide/openslide.git --branch=v4.0.0 --depth=1 \
    && cd openslide \
    && meson setup build_openslide --prefix=/usr/local -Dlibdir=lib \
    && meson compile -C build_openslide \
    && meson install -C build_openslide \
    && cd .. && rm -rf openslide \
    && echo "/usr/local/lib" > /etc/ld.so.conf.d/usr-local-lib.conf \
    && ldconfig

# Assuming iipsrv source is in a folder named 'iipsrv' relative to Dockerfile
COPY ./iipsrv /src/iipsrv

# add the patches
COPY ./patch/configure.ac /src/iipsrv/

COPY ./patch/ /src/iipsrv/src/patches_temp/
RUN cp /src/iipsrv/src/patches_temp/* /src/iipsrv/src/ && rm -rf /src/iipsrv/src/patches_temp/

RUN pkg-config --cflags --libs openslide

WORKDIR /src/iipsrv
RUN mkdir -p /src/iipsrv/m4
RUN ./autogen.sh && \
    ./configure --enable-openslide --enable-png && \
    make

RUN ls -l /src/iipsrv/src/iipsrv.fcgi

# --- Stage 2: Runtime ---
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Install only runtime dependencies and Apache
RUN apt-get update && apt-get install -y --no-install-recommends \
    apache2 \
    libapache2-mod-fcgid \
    libfcgi0ldbl \
    libjpeg-turbo8 \
    libmemcached11 \
    libtiff6 \
    libpng16-16 \
    libgomp1 \
    libglib2.0-0 \
    libcairo2 \
    libgdk-pixbuf-2.0-0 \
    libsqlite3-0 \
    libxml2 \
    libopenjp2-7 \
    libwebp7 \
    libwebpmux3 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Copy OpenSlide 4.0.0 and libdicom libraries built from source
COPY --from=builder /usr/local/lib/libopenslide* /usr/local/lib/
COPY --from=builder /usr/local/lib/libdicom* /usr/local/lib/
RUN echo "/usr/local/lib" > /etc/ld.so.conf.d/usr-local-lib.conf && ldconfig

# Enable Apache modules properly
RUN a2enmod rewrite fcgid proxy proxy_http

# Create directory for the FCGI binary
RUN mkdir -p /var/www/localhost/fcgi-bin/

# Copy the compiled binary from the builder stage
COPY --from=builder /src/iipsrv/src/iipsrv.fcgi /var/www/localhost/fcgi-bin/iipsrv.fcgi

# Copy your configurations
# Note: Ensure these files exist in your build context
COPY ./fcgid.conf /etc/apache2/mods-enabled/fcgid.conf
COPY ./apache2.conf /etc/apache2/apache2.conf
COPY ./ports.conf /etc/apache2/ports.conf

# Set permissions for Apache to execute the binary
RUN chown -R www-data:www-data /var/www/localhost/fcgi-bin/ && \
    chmod +x /var/www/localhost/fcgi-bin/iipsrv.fcgi

EXPOSE 80

CMD ["apachectl", "-D", "FOREGROUND"]
