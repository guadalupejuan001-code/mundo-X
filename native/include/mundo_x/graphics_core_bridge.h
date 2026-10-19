#ifndef MUNDO_X_GRAPHICS_CORE_BRIDGE_H
#define MUNDO_X_GRAPHICS_CORE_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable ABI shared by the desktop observer and Android JNI. COSMIC X Engine
 * is the package title; the existing Mundo X core and gate identifiers remain.
 * A renderer borrows a session handle. Only its owner destroys the session.
 * All functions synchronize access to the live session. Callers must still
 * prevent mx_destroy from racing another call or using an already closed handle.
 */
enum mx_backend_kind {
  MX_BACKEND_COMPACT = 0,
  MX_BACKEND_DENSE_REFERENCE = 1,
  MX_BACKEND_REDUCED_T = 2,
  MX_BACKEND_FACTORIZED = 3
};

enum mx_session_capability {
  MX_CAP_SESSION = 1u << 0,
  MX_CAP_READ_ONLY_PAULI = 1u << 1,
  MX_CAP_GATE_HISTORY = 1u << 2,
  MX_CAP_STABILIZER_GENERATORS = 1u << 3,
  MX_CAP_DENSE_REFERENCE = 1u << 4,
  MX_CAP_T_OPERATIONS = 1u << 5
};

/* The stable triples are [operation, first_qubit, second_qubit].
 * Unary operations ignore the third field, which canonical history stores as 0.
 */
enum mx_gate_kind {
  MX_GATE_H = 0, MX_GATE_S = 1, MX_GATE_X = 2, MX_GATE_Z = 3,
  MX_GATE_CX = 4, MX_GATE_CZ = 5, MX_GATE_Y = 6
};

const char* mx_last_error(void);
const char* mx_backend_name(int kind);
/* Capabilities/gate masks describe THIS shared session interface. The user's
 * ReducedExactT and Factorized implementations exist independently; a zero
 * mask for those ids means this interface does not silently select or expand
 * them into a different representation.
 */
uint64_t mx_capabilities(int kind);
uint64_t mx_gate_mask(int kind);

void* mx_create(int kind, size_t qubits);
void mx_destroy(void* session);
int mx_apply(void* session, const uint64_t* triples, size_t gate_count);
int mx_query(void* session, const char* const* paulis, size_t count, double* out);
int mx_generator(void* session, size_t row, char* out, size_t capacity);

size_t mx_qubits(void* session);
int mx_backend(void* session);
/* revision is the persisted nonempty-batch serial. epoch is a live-handle
 * invalidation serial: it also advances on restore, even if imported revision
 * equals the current revision. In-place renderer caches should use epoch.
 */
uint64_t mx_revision(void* session);
uint64_t mx_epoch(void* session);
uint64_t mx_rebuilds(void* session);
size_t mx_state_bytes(void* session);
size_t mx_observer_bytes(void* session);
size_t mx_history_bytes(void* session);

size_t mx_history_count(void* session);
/* Capacity is measured in gates (three uint64 values each), never bytes.
 * Returns 0 on success or -1 without partial writes on insufficient capacity.
 */
int mx_history(void* session, uint64_t* triples, size_t capacity_in_gates);
/* Atomic history and revision snapshot. Metadata-only query is permitted with
 * triples == NULL and capacity == 0. actual_count and revision are required.
 * When capacity is insufficient, actual_count/revision report the snapshot but
 * no history element is written. Retry if another writer grew the history.
 */
int mx_snapshot(void* session, uint64_t* triples, size_t capacity_in_gates,
                size_t* actual_count, uint64_t* revision);
/* Replay from |0...0> in a candidate of the same backend/qubit count, then
 * commit. Invalid input or allocation failure leaves the live state, revision,
 * history, epoch and observation cache unchanged. No implicit measurement.
 */
int mx_restore(void* session, const uint64_t* triples, size_t gate_count,
               uint64_t revision);

#ifdef __cplusplus
}
#endif
#endif
