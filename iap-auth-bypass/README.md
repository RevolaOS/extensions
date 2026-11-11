# iAP Auth Bypass

Based on [Accessory signature check bypass](https://github.com/lgreitis/osos-lab/commit/c99dc3309345e1206ea1cd71e218cce3168c4722) by RaduTek in RepriseOS.

Lets accessories use authentication-gated iPod Accessory Protocol (iAP) lingos and commands without an Apple authentication coprocessor. Both signature verifiers report success. Accessories must still answer the certificate and signature requests in the authentication handshake.

Enabled by default. It holds no memory beyond its module and loader bookkeeping and stores nothing of its own.

Disable it in Settings > Extensions to restore Apple's signature verification for subsequent handshakes. Disabling does not disconnect or revoke accessories that are already authenticated.

Build with `make -C iap-auth-bypass SDK=/path/to/revola-sdk` from the extensions repository, using SDK API 1.29 or later. Copy `iap-auth-bypass/build/iap-auth-bypass.rvx` to `/Revola/Extensions/iap-auth-bypass.rvx` on an iPod running core API 1.29 or later.

The [iAP reversing notes](https://github.com/RevolaOS/reversing/blob/main/docs/2.0.5/iap-auth.md) describe the handshake, verification gate, and hook calling conventions.
