// bench: the firmware's printf header is not wanted (the real printf of the host is); it brought LD_WORD / LD_DWORD in (via itu.h), so they come from here
#pragma once
#include <stdio.h>
#include "integer.h"
