# iAP Auth Bypass

Based on [Accessory signature check bypass](https://github.com/lgreitis/osos-lab/commit/c99dc3309345e1206ea1cd71e218cce3168c4722) by RaduTek in RepriseOS.

Lets accessories use authentication-gated iPod Accessory Protocol (iAP) lingos and commands without an Apple authentication coprocessor. Both signature verifiers report success. Accessories must still answer the certificate and signature requests in the authentication handshake.

Disabling it restores Apple's signature verification for later handshakes. It does not disconnect or revoke accessories that are already authenticated.

The handshake, the verification gate and the hook calling conventions are in the [iAP reversing notes](https://github.com/RevolaOS/reversing/blob/main/docs/2.0.5/iap-auth.md).

| | |
|---|---|
| Default | on |
| Memory | image only |
| Settings | none |
| Minimum SDK | 1.29 |
