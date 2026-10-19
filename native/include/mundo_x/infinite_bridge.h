#ifndef MUNDO_X_INFINITE_BRIDGE_H
#define MUNDO_X_INFINITE_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Additive InfiniteCore ABI. Existing mx_* finite sessions are unchanged.
 * One owner destroys a handle; other consumers may borrow it. Calls on a live
 * handle are synchronized, but destruction must not race any borrowed call.
 * Background ids: 0 = Zero, 1 = BellPairs. All counters use uint64_t.
 */
typedef struct mx_infinite_stats {
  uint64_t active_qubits;
  uint64_t components;
  uint64_t largest_component;
  uint64_t tableau_bytes;
  uint64_t address_bytes;
  uint64_t revision;
  uint64_t epoch;
  uint64_t background;
} mx_infinite_stats;

const char* mxi_last_error(void);
void* mxi_create(int background);
void mxi_destroy(void* session);

/* All status functions return 0 on success and -1 on failure. Output arguments
 * are unchanged on failure. execute accepts the shared .xi line grammar and
 * commits the complete request atomically, including measurements and RNG.
 * Results are JSON lines; gates and successful assertions produce no lines.
 * A returned string belongs to the caller and must use mxi_free_string.
 */
int mxi_execute(void* session, const char* program, char** out);
int mxi_snapshot(void* session, char** out);
int mxi_restore(void* session, const char* snapshot);
int mxi_reset(void* session, int background);

/* Sparse observables use "[-|+] X:address Z:address" (also I and Y).
 * Empty text denotes +identity. Values, revision and epoch refer to the same
 * locked state. Querying does not materialize, measure or advance the RNG.
 * revision and epoch are required even when count is zero.
 */
int mxi_query(void* session, const char* const* observables, size_t count,
              double* out, uint64_t* revision, uint64_t* epoch);
int mxi_get_stats(void* session, mx_infinite_stats* out);
void mxi_free_string(char* text);

#ifdef __cplusplus
}
#endif
#endif
