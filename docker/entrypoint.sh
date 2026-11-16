#!/bin/sh
# Generates a self-signed certificate for $DOOM_HOST on first start (or when
# the host changes), then starts Caddy with the command passed as arguments.
set -eu

CERT_DIR="${CERT_DIR:-/srv/certs}"
mkdir -p "$CERT_DIR"

HOST="${DOOM_HOST:-localhost}"

need_new=1
if [ -f "$CERT_DIR/ssl.crt" ] && [ -f "$CERT_DIR/host.txt" ]; then
    if [ "$(cat "$CERT_DIR/host.txt")" = "$HOST" ]; then
        need_new=0
    fi
fi

if [ "$need_new" = "1" ]; then
    # A host made only of digits, dots and colons is an IP address;
    # anything else is treated as a DNS name. Localhost entries are always
    # included so the certificate also works for local access.
    case "$HOST" in
        *[!0-9a-fA-F.:]*)
            SAN="DNS:$HOST,DNS:localhost,IP:127.0.0.1"
            ;;
        *)
            SAN="IP:$HOST,DNS:localhost,IP:127.0.0.1"
            ;;
    esac

    # X.509 certificates must have an expiry date, so use 100 years, which is
    # effectively never for any practical purpose.
    openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout "$CERT_DIR/ssl.key" \
        -out "$CERT_DIR/ssl.crt" \
        -days 36500 \
        -subj "/CN=$HOST" \
        -addext "subjectAltName=$SAN"
    echo "$HOST" > "$CERT_DIR/host.txt"
    echo "Generated self-signed certificate for $HOST in $CERT_DIR"
fi

exec "$@"
