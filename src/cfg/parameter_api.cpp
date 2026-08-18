#include "parameter_api.h"

#include <string>

#include "parameter.h"

int32_t param_i_get(const ParamInt *p) {
    return p->get();
}

void param_i_set(ParamInt *p, int32_t v) {
    p->set(v);
}

float param_f_get(const ParamFloat *p) {
    return p->get();
}

void param_f_set(ParamFloat *p, float v) {
    p->set(v);
}

const char *param_t_get(const ParamText *p) {
    static thread_local std::string buf;
    buf = p->get();
    return buf.c_str();
}

void param_t_set(ParamText *p, const char *v) {
    std::string sv = v ? v : "";
    p->set(sv);
}
