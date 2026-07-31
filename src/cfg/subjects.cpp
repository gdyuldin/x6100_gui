#include "subjects.h"

extern "C" {
    #include "../lvgl/lvgl.h"
    #include <stdint.h>
    #include <stdio.h>
    #include <stdlib.h>
}

void Observer::notify() {
    if (fn && subj) {
        fn(subj, user_data);
    }
}

void Observer::unsubscribe() {
    if (subj) {
        subj->unsubscribe(this);
        subj = nullptr;
    }
};

void ObserverDelayed::notify() {
    lv_async_call([](void* user_data) {
        auto* obs = static_cast<ObserverDelayed*>(user_data);
        obs->Observer::notify();
    }, this);
}

Observer* Subject::subscribe(observer_cb fn, void *user_data) {
    const std::lock_guard<std::mutex> lock(mutex_subscribe);

    auto observer = new Observer(this, fn, user_data);
    observers.push_back(observer);
    return observer;
}

ObserverDelayed* Subject::subscribe_delayed(observer_cb fn, void *user_data) {
    const std::lock_guard<std::mutex> lock(mutex_subscribe);

    auto observer = new ObserverDelayed(this, fn, user_data);
    observers.push_back(observer);
    return observer;
}

void Subject::unsubscribe(Observer *observer) {
    const std::lock_guard<std::mutex> lock(mutex_subscribe);
    observers.erase(std::find(observers.begin(), observers.end(), observer));
}

void Subject::set_pause_notify(bool val) {
    this->pause_notify = val;
}

void Subject::force_paused_notify() {
    if (this->pause_notify && this->changed) {
        this->notify();
    }
    this->changed = false;
    this->pause_notify = false;
}

void Subject::notify() {
    if (is_notifying) {
        LV_LOG_ERROR("Already notifying: %p", this);
    }
    is_notifying = true;
    std::vector<Observer*> observers_copy;
    {
        const std::lock_guard<std::mutex> lock(mutex_subscribe);
        observers_copy = observers;
    }
    for (auto& observer : observers_copy) {
        observer->notify();
    }
    is_notifying = false;
}

data_type Subject::dtype() {
    return DTYPE_INVALID;
}

/**
 * String subject
 */


char *SubjectT<const char *>::get() {
    const std::lock_guard<std::mutex> lock(mutex);
    return strdup(val.data());
}

void SubjectT<const char *>::set(const char *data) {
    std::string new_val(data);
    bool        changed = false;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (this->val != new_val) {
            changed   = true;
            this->val = new_val;
        }
    }
    if (changed) {
        if (this->pause_notify) {
            this->changed = true;
        } else {
            this->notify();
        }
    }
}

SubjectsUpdateLock::SubjectsUpdateLock(std::initializer_list<Subject *> args) {
    subjects = args;
    for (const auto& s: subjects) {
        s->set_pause_notify(true);
    }

}

SubjectsUpdateLock::~SubjectsUpdateLock() {
    for (const auto& s: subjects) {
        s->force_paused_notify();
    }
}

/*
    C-API
*/


Subject *subject_create_int(int32_t val) {
    return new SubjectT(val);
}

Subject *subject_create_uint64(uint64_t val) {
    return new SubjectT(val);
}

Subject *subject_create_float(float val) {
    return new SubjectT(val);
}

Subject *subject_create_text(const char *val) {
    return new SubjectT(val);
}

int32_t subject_get_int(Subject *subj) {
    return static_cast<SubjectInt*>(subj)->get();
}

uint64_t subject_get_uint64(Subject *subj) {
    return static_cast<SubjectUint64*>(subj)->get();
}

float subject_get_float(Subject *subj) {
    return static_cast<SubjectFloat*>(subj)->get();
}

char *subject_get_text(Subject *subj) {
    return static_cast<SubjectText*>(subj)->get();
}

void subject_set_int(Subject *subj, int32_t val) {
    if (subj->dtype() == DTYPE_INT) {
        static_cast<SubjectInt*>(subj)->set(val);
    } else {
        LV_LOG_ERROR("Expected subject with dtype int, got %u\n", subj->dtype());
    }
}
void subject_set_uint64(Subject *subj, uint64_t val) {
    if (subj->dtype() == DTYPE_UINT64) {
        static_cast<SubjectUint64*>(subj)->set(val);
    } else {
        LV_LOG_ERROR("Expected subject with dtype uint64, got %u\n", subj->dtype());
    }
}

void subject_set_float(Subject *subj, float val) {
    if (subj->dtype() == DTYPE_FLOAT) {
        static_cast<SubjectFloat*>(subj)->set(val);
    } else {
        LV_LOG_ERROR("Expected subject with dtype float, got %u\n", subj->dtype());
    }
}

void subject_set_text(Subject *subj, const char *val) {
    if (subj->dtype() == DTYPE_STR) {
        static_cast<SubjectText*>(subj)->set(val);
    } else {
        LV_LOG_ERROR("Expected subject with dtype str, got %u\n", subj->dtype());
    }
}

void observer_clear(Observer *o) {
    if (o) {
        o->unsubscribe();
        delete o;
    }
}

void observer_delayed_clear(ObserverDelayed *o) {
    if (o) {
        o->unsubscribe();
        delete o;
    }
}

Observer *subject_add_observer(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe(fn, user_data);
}

Observer *subject_add_observer_and_call(Subject *subj, observer_cb fn, void *user_data) {
    auto observer = subj->subscribe(fn, user_data);
    fn(subj, user_data);
    return observer;
}

ObserverDelayed *subject_add_delayed_observer(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe_delayed(fn, user_data);
}

ObserverDelayed *subject_add_delayed_observer_and_call(Subject *subj, observer_cb fn, void *user_data) {
    auto observer = subj->subscribe_delayed(fn, user_data);
    fn(subj, user_data);
    return observer;
}

void subject_del_observer(Subject *subj, Observer *o) {
    subj->unsubscribe(o);
}

void subject_del_observer_delayed(Subject *subj, ObserverDelayed *o) {
    subj->unsubscribe(o);
}

// void subject_int_derived_add_parent(Subject *subj, Subject *parent) {
//     static_cast<SubjectDerivedInt*>(subj)->add_parent_subj(parent);
// }

data_type subject_get_dtype(Subject *subj) {
    return subj->dtype();
}

