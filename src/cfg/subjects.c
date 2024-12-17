#include "subjects.private.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void init_observers(subject_t subj);
static void call_observers(subject_t subj);
static void notify_group(subject_t subj, void *user_data);

subject_t subject_create_int(int32_t val) {
    subject_t subj = malloc(sizeof(struct __subject));
    pthread_mutex_init(&subj->mutex_get, NULL);
    pthread_mutex_init(&subj->mutex_set, NULL);
    pthread_mutex_init(&subj->mutex_subscribe, NULL);
    subj->int_val = val;
    subj->dtype = DTYPE_INT;
    init_observers(subj);
    return subj;
}

subject_t subject_create_uint64(uint64_t val) {
    subject_t subj = malloc(sizeof(struct __subject));
    pthread_mutex_init(&subj->mutex_get, NULL);
    pthread_mutex_init(&subj->mutex_set, NULL);
    pthread_mutex_init(&subj->mutex_subscribe, NULL);
    subj->uint64_val = val;
    subj->dtype = DTYPE_UINT64;
    init_observers(subj);
    return subj;
}

subject_t subject_create_group(subject_t *subjects, uint8_t count) {
    subject_t subj = malloc(sizeof(struct __subject));
    pthread_mutex_init(&subj->mutex_get, NULL);
    pthread_mutex_init(&subj->mutex_set, NULL);
    pthread_mutex_init(&subj->mutex_subscribe, NULL);
    subj->dtype = DTYPE_GROUP;
    init_observers(subj);
    subj->group = malloc(sizeof(*subj->group));
    subj->group->subjects = malloc(sizeof(*subj->group->subjects) * count);
    subj->group->count = count;
    for (uint8_t i = 0; i < count; i++) {
        subj->group->subjects[i] = subjects[i];
        subject_add_observer(subjects[i], notify_group, subj);
    }
    return subj;
}

int32_t subject_get_int(subject_t subj) {
    if (subj->dtype != DTYPE_INT)
        fprintf(stderr, "WARNING: subject dtype (%d) is not INT, get result might be wrong\n", subj->dtype);
    pthread_mutex_lock(&subj->mutex_get);
    int32_t val = subj->int_val;
    pthread_mutex_unlock(&subj->mutex_get);
    return val;
}

uint64_t subject_get_uint64(subject_t subj) {
    if (subj->dtype != DTYPE_UINT64)
        fprintf(stderr, "WARNING: subject dtype (%d) is not UINT64, get result might be wrong\n", subj->dtype);
    pthread_mutex_lock(&subj->mutex_get);
    uint64_t val = subj->uint64_val;
    pthread_mutex_unlock(&subj->mutex_get);
    return val;
}

observer_t subject_add_observer(subject_t subj, void (*fn)(subject_t, void *), void *user_data) {
    if (subj == NULL) {
        fprintf(stderr, "Subject is null\n");
        return NULL;
    }
    pthread_mutex_lock(&subj->mutex_subscribe);
    bool added = false;
    uint8_t i;
    observer_t observer;
    for (i = 0; i < MAX_OBSERVERS; i++) {
        if (!subj->observers[i]) {
            subj->observers[i] = fn;
            subj->user_data[i] = user_data;
            added = true;
            break;
        }
    }
    pthread_mutex_unlock(&subj->mutex_subscribe);
    if (!added) {
        printf("WARNING: No free slots for observer, will not subscribe\n");
        observer = NULL;
    } else {
        observer = malloc(sizeof(struct __observer));
        observer->cb_id = i;
        observer->subj = subj;
    }
    return observer;
}

observer_t subject_add_observer_and_call(subject_t subj, void (*fn)(subject_t, void *), void *user_data) {
    observer_t observer = subject_add_observer(subj, fn, user_data);
    if (observer)
        fn(subj, user_data);
    return observer;
}

void subject_set_int(subject_t subj, int32_t val) {
    if (subj->dtype != DTYPE_INT)
        printf("WARNING: subject dtype (%d) is not INT, set result might be wrong\n", subj->dtype);
    pthread_mutex_lock(&subj->mutex_set);
    if (subj->int_val != val) {
        subj->int_val = val;
        call_observers(subj);
    }
    pthread_mutex_unlock(&subj->mutex_set);
}

void subject_set_uint64(subject_t subj, uint64_t val) {
    if (subj->dtype != DTYPE_UINT64)
        printf("WARNING: subject dtype (%d) is not UINT64, set result might be wrong\n", subj->dtype);
    pthread_mutex_lock(&subj->mutex_set);
    if (subj->uint64_val != val) {
        subj->uint64_val = val;
        call_observers(subj);
    }
    pthread_mutex_unlock(&subj->mutex_set);
}

void observer_remove(observer_t observer) {
    if (observer) {
        observer->subj->observers[observer->cb_id] = NULL;
        observer->subj->user_data[observer->cb_id] = NULL;
    }
    free(observer);
}

static void init_observers(subject_t subj) {
    for (size_t i = 0; i < MAX_OBSERVERS; i++) {
        subj->observers[i] = NULL;
        subj->user_data[i] = NULL;
    }
}

static void call_observers(subject_t subj) {
    for (size_t i = 0; i < MAX_OBSERVERS; i++) {
        if (subj->observers[i]) {
            subj->observers[i](subj, subj->user_data[i]);
        } else {
            break;
        }
    }
}

static void notify_group(subject_t subj, void *user_data) {
    subject_t group = (subject_t)user_data;
    call_observers(group);
}
