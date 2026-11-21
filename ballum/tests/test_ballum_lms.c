#include "sleela_ballum/ballum_lms.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    static ballum_lms_key_t k;
    uint8_t seed[32], id[16], sig[BALLUM_LMS_SIG_BYTES], sig2[BALLUM_LMS_SIG_BYTES];
    for (int i = 0; i < 32; ++i) seed[i] = (uint8_t)(i * 7 + 1);
    for (int i = 0; i < 16; ++i) id[i] = (uint8_t)(0xA0 + i);
    CHECK(ballum_lms_keygen(NULL, seed, id) == BALLUM_STATUS_INVALID_ARGUMENT);
    CHECK(ballum_lms_keygen(&k, seed, id) == BALLUM_STATUS_OK);
    CHECK(k.pub[3] == 5 && k.pub[7] == 4);                       /* type codes */
    uint8_t pub[BALLUM_LMS_PUBKEY_BYTES];
    memcpy(pub, k.pub, sizeof(pub));

    const uint8_t msg[] = "audit head 0123456789abcdef";
    CHECK(ballum_lms_sign(&k, msg, sizeof(msg), sig) == BALLUM_STATUS_OK);
    CHECK(ballum_lms_verify(pub, msg, sizeof(msg), sig, sizeof(sig)) == 1);

    /* wrong message, wrong length, truncated, wrong key */
    uint8_t other[sizeof(msg)]; memcpy(other, msg, sizeof(msg)); other[0] ^= 1;
    CHECK(ballum_lms_verify(pub, other, sizeof(other), sig, sizeof(sig)) == 0);
    CHECK(ballum_lms_verify(pub, msg, sizeof(msg) - 1, sig, sizeof(sig)) == 0);
    CHECK(ballum_lms_verify(pub, msg, sizeof(msg), sig, sizeof(sig) - 1) == 0);
    uint8_t pub2[BALLUM_LMS_PUBKEY_BYTES]; memcpy(pub2, pub, sizeof(pub)); pub2[30] ^= 1;
    CHECK(ballum_lms_verify(pub2, msg, sizeof(msg), sig, sizeof(sig)) == 0);

    /* flipping any single bit of the signature breaks it (sampled across the whole) */
    for (size_t i = 0; i < BALLUM_LMS_SIG_BYTES; i += 37) {
        memcpy(sig2, sig, sizeof(sig)); sig2[i] ^= 0x10;
        CHECK(ballum_lms_verify(pub, msg, sizeof(msg), sig2, sizeof(sig2)) == 0);
    }

    /* empty message, each leaf signs once and gives a distinct signature */
    CHECK(ballum_lms_sign(&k, NULL, 0, sig2) == BALLUM_STATUS_OK);
    CHECK(ballum_lms_verify(pub, NULL, 0, sig2, sizeof(sig2)) == 1);
    CHECK(memcmp(sig, sig2, 4) != 0 || memcmp(sig, sig2, sizeof(sig)) != 0);
    CHECK(sig[3] == 0 && sig2[3] == 1);                            /* leaf numbers 0 then 1 */

    /* every remaining leaf verifies, then the key is exhausted and refuses */
    for (uint32_t i = 2; i < BALLUM_LMS_LEAVES; ++i) {
        CHECK(ballum_lms_sign(&k, msg, sizeof(msg), sig2) == BALLUM_STATUS_OK);
        CHECK(sig2[3] == i);
        CHECK(ballum_lms_verify(pub, msg, sizeof(msg), sig2, sizeof(sig2)) == 1);
    }
    CHECK(ballum_lms_sign(&k, msg, sizeof(msg), sig2) == BALLUM_STATUS_PERMISSION_DENIED);

    ballum_lms_wipe(&k);
    for (size_t i = 0; i < sizeof(k); ++i) CHECK(((uint8_t *)&k)[i] == 0);
    puts("test_ballum_lms: ok");
    return 0;
}
