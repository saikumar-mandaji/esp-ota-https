# `example_ca_cert.pem`

This is a **throwaway, self-signed EXAMPLE certificate** generated solely so
this repository builds and demonstrates the `EMBED_TXTFILES` + TLS
cert-pinning pattern end to end (`openssl req -x509 -newkey rsa:2048 -nodes
... -subj "/CN=example-ota-server.invalid"`, 10-year validity). Its private
key was discarded immediately after signing and was never committed anywhere.

**It is not usable to secure anything.** Before deploying this project
against a real update server:

1. Obtain (or generate) the CA certificate that actually signs your OTA
   server's TLS certificate. See `docs/hardware/BOM.md` for how to stand up
   a test HTTPS server with a self-signed cert and get its CA cert out.
2. Replace this file with that real CA certificate (same filename, or update
   `main/CMakeLists.txt`'s `EMBED_TXTFILES` line and the `asm("_binary_..._start")`
   symbol names in `main/main.c` to match).
3. Never point production devices at a build that still embeds this example
   certificate -- the TLS handshake against your real server will simply
   fail closed (safe, but not useful), which is a signal to check this file.

`*.pem` is listed in `.gitignore` for anything you generate locally going
forward (real keys, real server certs) -- this specific example file is
intentionally tracked so the repository builds standalone.
