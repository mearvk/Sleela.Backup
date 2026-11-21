/*
 * Hash-based signatures for the audit trail: LMS (RFC 8554, also NIST SP 800-208) with
 * SHA-256, tree height 5 (32 signatures per key), Winternitz w=8 (LM-OTS).
 *
 * Why this and not ML-DSA: ML-DSA (FIPS 204) is not implemented in this repository,
 * and crypto/PQ-INTEGRATION.md says it should be vendored from an audited source, not
 * re-derived. LMS needs only SHA-256, which the repository already has, so a verifier
 * with the public key can check an audit head today.
 *
 * VALIDATION STATUS: written from the RFC text and tested here for round trip, tamper
 * detection, key exhaustion and bit flips. It has NOT been checked against the RFC 8554
 * test vectors (they could not be fetched in the authoring environment), so
 * interoperability with other LMS implementations is unconfirmed. Add those vectors
 * before relying on it outside this project.
 *
 * STATE: LMS is stateful. Every signature uses one leaf, and reusing a leaf breaks the
 * scheme. ballum_lms_sign() advances the counter before it returns the signature and
 * refuses when all 32 leaves are used. Persist the key (it holds the seed) after
 * every signature, never restore an older copy, and never copy a live key.
 * The private key is a secret: zero it with ballum_lms_wipe() when done.
 */
#ifndef SLEELA_BALLUM_LMS_H
#define SLEELA_BALLUM_LMS_H

#include "ballum_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BALLUM_LMS_HEIGHT       5u
#define BALLUM_LMS_LEAVES       (1u << BALLUM_LMS_HEIGHT)
#define BALLUM_LMS_PUBKEY_BYTES 56u      /* lms type, lmots type, I (16), root (32) */
#define BALLUM_LMS_SIG_BYTES    1292u    /* q, lmots sig (1124), lms type, path (5*32) */

typedef struct {
    uint8_t  seed[32];
    uint8_t  id[16];
    uint32_t next;                       /* next unused leaf, 0..32 */
    uint8_t  leaf[BALLUM_LMS_LEAVES][32];/* public leaf values, kept to build paths */
    uint8_t  node[2 * BALLUM_LMS_LEAVES][32];
    uint8_t  pub[BALLUM_LMS_PUBKEY_BYTES];
} ballum_lms_key_t;

/* Build a key from a 32-byte secret seed and a 16-byte key id. Both come from the
 * caller's own randomness. */
ballum_status_t ballum_lms_keygen(ballum_lms_key_t *key, const uint8_t seed[32], const uint8_t id[16]);

/* Sign msg with the next unused leaf. BUFFER_TOO_SMALL-free: out must hold
 * BALLUM_LMS_SIG_BYTES. Returns PERMISSION_DENIED when every leaf has been used. */
ballum_status_t ballum_lms_sign(ballum_lms_key_t *key, const uint8_t *msg, size_t len,
                                uint8_t out[BALLUM_LMS_SIG_BYTES]);

/* 1 when sig is a valid signature of msg under pub, 0 otherwise. Any malformed input
 * is simply invalid. */
int ballum_lms_verify(const uint8_t pub[BALLUM_LMS_PUBKEY_BYTES], const uint8_t *msg, size_t len,
                      const uint8_t *sig, size_t sig_len);

/* Zero the whole key. */
void ballum_lms_wipe(ballum_lms_key_t *key);

#ifdef __cplusplus
}
#endif

#endif /* SLEELA_BALLUM_LMS_H */
