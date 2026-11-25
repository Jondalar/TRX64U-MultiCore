// Forced into every native unit build (tests/coreapp/run.sh, -include): the Atari 800 XL's descriptor is defined by its library (cores/atari800/app/lib/atari800.cc), no longer declared by the shared
// cores.h (the shared app's table has no row of it), and the shared units that use it as their sample core name it. Only the declaration, no include: desc.h must still come in at the unit's own place
// (compiled_table.cc renames core_table before it).
#ifdef __cplusplus
struct CoreDesc;
extern const CoreDesc atari800_desc;
#endif
