The zip library (T197). `src/` is miniz (MIT), moved here from `apps/unzip/src/miniz` (copy of `apps/ultimate/apps/unzip/src/miniz`, submodule commit `aa2ef5dc`), unchanged.
`miniz_lib.cc` is `lib_register`: it puts the call table of `zip_lib.h` into the record (`LibRecord::exports`); an application reaches it with
`api->lib->find("miniz", ZIPLIB_VERSION, &size)` (base 3.1) and carries only `zip_lib.h` and miniz's headers for the types. Format 3, resident, built by `app/lib/app.mk`
with `APP_KIND = lib`; ships as `miniz.u2a` beside `unzip.u2a`.
