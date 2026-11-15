# Self-hosted wasmdoom.
#
# Serves the pre-built static site from public/ (no build step) over HTTPS
# with a self-signed certificate generated on first start.

FROM caddy:2-alpine

# Full OpenSSL for generating the self-signed certificate at startup.
RUN apk add --no-cache openssl

# Pre-built engine and page; no source or toolchain needed in the image.
COPY public/ /srv/wasmdoom/

COPY docker/Caddyfile /etc/caddy/Caddyfile
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

# Address or domain the certificate is generated for; set it in
# docker-compose.yml (or a .env file) to match how you reach the site.
ENV DOOM_HOST=localhost

EXPOSE 443

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["caddy", "run"]
