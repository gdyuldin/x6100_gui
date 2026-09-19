#pragma once

// C-compatible API for the Subject / SubjectT / Observer pub-sub subsystem.
// The C++ types (Subject, SubjectT<T>, Observer, ObserverDelayed) live in
// subject.h and cannot be parsed by a C compiler, so this C-safe header exposes
// opaque handles plus the generic subject helpers consumed by C code. Pure
// subject consumers (radio.h, app_ports.cpp, buttons.h) include this header
// instead of the heavy cfg_api.h to avoid pulling in parameter.h/db.h.
//
// The opaque handles below resolve to the real C++ types in C++ builds
// (subject.h is not a namespace, so no `using` is added for Subject/Observer).
//
// Ownership: observers returned by subject_*_subscribe are borrowed: the
// Subject owns one reference while the observer stays subscribed; C/UI code
// releases that reference with param_unsubscribe. The user_data is the caller's
// own data — param_unsubscribe never frees it. In C++ wrap the returned pointer
// in a Subscription.

#include <stdint.h>

#ifdef __cplusplus
#include "subject.h"

using SubjectInt   = SubjectT<int32_t>;
using SubjectFloat = SubjectT<float>;
#else
// Opaque C handles (resolved to real C++ types in C++ builds).
typedef struct Subject      Subject;
typedef struct SubjectInt   SubjectInt;
typedef struct SubjectFloat SubjectFloat;
typedef void (*observer_cb)(Subject *, void *);
typedef struct Observer        Observer;
typedef struct ObserverDelayed ObserverDelayed;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Unsubscribe an observer and release its reference. The observer is destroyed
// when no other reference is held (a Subscription keeps it alive). The
// user_data is the caller's own data — param_unsubscribe never frees it.
void param_unsubscribe(Observer *o);

// --- Generic Subject helpers (UI state, not persisted) ---
SubjectInt   *subject_i_create(int32_t val);
int32_t       subject_i_get(SubjectInt *subj);
void          subject_i_set(SubjectInt *subj, int32_t val);
SubjectFloat *subject_f_create(float val);
float         subject_f_get(SubjectFloat *subj);
void          subject_f_set(SubjectFloat *subj, float val);

Observer *subject_subscribe(Subject *subj, observer_cb fn, void *user_data);
Observer *subject_subscribe_and_notify(Subject *subj, observer_cb fn, void *user_data);

ObserverDelayed *subject_subscribe_delayed(Subject *subj, observer_cb fn, void *user_data);
ObserverDelayed *subject_subscribe_delayed_and_notify(Subject *subj, observer_cb fn, void *user_data);

// Drain the delayed-observer queue. Must be called from the main thread
// periodically (same loop as lv_timer_handler / scheduler_work).
void observer_delayed_drain(void);

// Clear the delayed-observer queue at shutdown without invoking callbacks.
// Call once from the main thread after all producer threads have stopped, so
// pending ObserverDelayed references are released before the process exits.
void observer_delayed_shutdown(void);

#ifdef __cplusplus
} // extern "C"
#endif
