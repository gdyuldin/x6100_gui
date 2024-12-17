#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MAX_OBSERVERS 16


typedef struct __subject *subject_t;

typedef struct __observer *observer_t;


subject_t subject_create_int(int32_t val);
subject_t subject_create_uint64(uint64_t val);
subject_t subject_create_group(subject_t *subjects, uint8_t count);

int32_t  subject_get_int(subject_t subj);
uint64_t subject_get_uint64(subject_t subj);

/// @brief Add observer to subject
/// @param subj subject to add observer
/// @param fn observer callback
/// @param user_data pointer to additional user data
/// @return observer to remove it from subject
observer_t subject_add_observer(subject_t subj, void (*fn)(subject_t, void *), void *user_data);

observer_t subject_add_observer_and_call(subject_t subj, void (*fn)(subject_t, void *), void *user_data);

void subject_set_int(subject_t subj, int32_t val);
void subject_set_uint64(subject_t subj, uint64_t val);

void observer_remove(observer_t observer);
