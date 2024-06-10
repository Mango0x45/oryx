#ifndef ORYX_ERRORS_H
#define ORYX_ERRORS_H

#include <stdnoreturn.h>

noreturn void err(const char *, ...) __attribute__((format(printf, 1, 2)));

#endif /* !ORYX_ERRORS_H */
