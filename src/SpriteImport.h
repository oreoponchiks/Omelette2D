#pragma once
#include "Sandbox.h"
#include <stddef.h>
/* UTF-8 XML path. Failure leaves both output values unchanged. */
bool sprite_import_xml(const char* path, SandboxShape* shape, bool* fixed, char* error,
                       size_t error_size);
