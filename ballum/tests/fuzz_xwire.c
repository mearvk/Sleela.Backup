/* libFuzzer harness for the X11 wire framer. The framer parses bytes from an
 * untrusted peer, so any input must be consumed without a crash, hang or
 * out-of-bounds access. Build with `make fuzz` (clang -fsanitize=fuzzer) or
 * `make fuzz-smoke` (standalone driver, fixed pseudo-random corpus). */
#include "sleela_ballum/ballum_xwire.h"
#include <stdint.h>
#include <stdlib.h>

static int  cb_request(void *u, const ballum_xreq_t *r, uint32_t *s) { (void)u; (void)r; *s = 0; return 1; }
static void cb_reply(void *u, const ballum_xreply_t *r) { (void)u; (void)r; }
static void cb_event(void *u, const uint8_t *e, size_t n, int be) { (void)u; (void)e; (void)n; (void)be; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ballum_xwire_cb_t cb = { cb_request, cb_reply, cb_event, NULL };
    ballum_xwire_t *w = ballum_xwire_create(&cb);
    if (!w) return 0;
    /* First byte of each chunk picks the direction; the next two its length. */
    size_t i = 0;
    while (i + 3 <= size) {
        int to_server = data[i] & 1;
        size_t n = ((size_t)data[i + 1] | ((size_t)data[i + 2] << 8)) % 512u;
        i += 3;
        if (n > size - i) n = size - i;
        if (to_server) ballum_xwire_client_feed(w, data + i, n);
        else           ballum_xwire_server_feed(w, data + i, n);
        i += n;
    }
    (void)ballum_xwire_desynced(w);
    ballum_xwire_destroy(w);
    return 0;
}

#ifdef BALLUM_FUZZ_STANDALONE
int main(void) {
    uint32_t s = 0x1234567u;
    static uint8_t buf[4096];
    for (int iter = 0; iter < 20000; ++iter) {
        size_t n = 1 + (s % sizeof(buf));
        for (size_t k = 0; k < n; ++k) { s = s * 1664525u + 1013904223u; buf[k] = (uint8_t)(s >> 24); }
        LLVMFuzzerTestOneInput(buf, n);
    }
    return 0;
}
#endif
