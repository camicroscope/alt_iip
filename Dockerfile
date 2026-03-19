# --- Stage 1: Build ---
FROM ubuntu:22.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update 
RUN apt-get install -y --no-install-recommends \
    build-essential \
    libgomp1 \
    autoconf \
    automake \
    libtool \
    pkg-config \
    libfcgi-dev \
    openslide-tools \
    libopenslide-dev \
    pkg-config \
    libjpeg-turbo8-dev \
    libmemcached-dev \
    libtiff-dev \
    libpng-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

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
FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive

# Install only runtime dependencies and Apache
RUN apt-get update && apt-get install -y --no-install-recommends \
    apache2 \
    libapache2-mod-fcgid \
    libfcgi0ldbl \
    libopenslide0 \
    libjpeg-turbo8 \
    libmemcached11 \
    libtiff5 \
    libpng16-16 \
    libgomp1 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

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