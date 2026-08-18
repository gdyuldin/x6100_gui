#include "subject_api.h"

#include "subject.h"

// Generic subject helpers and the shared observer destroyer. These wrap the
// C++ Subject/SubjectT/Observer types in subject.h.

void param_unsubscribe(Observer *o) {
    if (!o) {
        return;
    }
    ObserverDeleter{}(o);
}

SubjectInt *subject_i_create(int32_t val) {
    return new SubjectInt(val);
}

int32_t subject_i_get(SubjectInt *subj) {
    return subj->get();
}

void subject_i_set(SubjectInt *subj, int32_t val) {
    subj->set(val);
}

SubjectFloat *subject_f_create(float val) {
    return new SubjectFloat(val);
}

float subject_f_get(SubjectFloat *subj) {
    return subj->get();
}

void subject_f_set(SubjectFloat *subj, float val) {
    subj->set(val);
}

Observer *subject_subscribe(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe(fn, user_data);
}

Observer *subject_subscribe_and_notify(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe_and_notify(fn, user_data);
}

ObserverDelayed *subject_subscribe_delayed(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe_delayed(fn, user_data);
}

ObserverDelayed *subject_subscribe_delayed_and_notify(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe_delayed_and_notify(fn, user_data);
}
