// The second source file of the example: a helper that the operation calls. A library may have any number of files; LIB_SRCS lists them.
#ifndef EXAMPLE_GREET_H
#define EXAMPLE_GREET_H

// Writes "Hello, <name>!" into out (at most outlen bytes, NUL included) and returns the length written, without the NUL.
int greet_text(const char *name, char *out, int outlen);

#endif
