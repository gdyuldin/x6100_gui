#include "computed_api.h"
#include "computed_parameter.h"

#include <cstdint>
#include <string>

void cparam_i_set(ComputedParamInt *p, int32_t value) {
    p->set(value);
}

int32_t cparam_i_get(const ComputedParamInt *p) {
    return p->get();
}

void cparam_f_set(ComputedParamFloat *p, float value) {
    p->set(value);
}

float cparam_f_get(const ComputedParamFloat *p) {
    return p->get();
}

void cparam_t_set(ComputedParamText *p, const char *value) {
    std::string sv = value ? value : "";
    p->set(sv);
}
