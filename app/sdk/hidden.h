/* Included first into every file of an application (app/lib/app.mk, -include): every declaration in it is hidden, so a symbol of another file is reached pc-relative
   and not through the global offset table - a table the loader does not fill (T104.1 D10). */
#pragma GCC visibility push(hidden)
