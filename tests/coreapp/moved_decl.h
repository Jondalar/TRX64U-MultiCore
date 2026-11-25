// Forced into every native unit build (tests/coreapp/run.sh, -include): the descriptors of the cores that moved to their own app (the Atari 800 XL: cores/atari800/app/lib/atari800.cc, the NES:
// cores/nes/app/lib/nes.cc, the Commodore 264: cores/c264/app/lib/c264.cc, the ZX Spectrum Next: cores/zxnext/app/lib/zxnext.cc, the Neo Geo: cores/neogeo/app/lib/neogeo.cc) are defined by their libraries, no longer declared by the shared cores.h (the shared app's table has no row of them), and the shared units that use them as their sample cores
// name them. Only the declarations, no include: desc.h must still come in at the unit's own place (compiled_table.cc renames core_table before it).
#ifdef __cplusplus
struct CoreDesc;
extern const CoreDesc atari800_desc;
extern const CoreDesc nes_desc;
extern const CoreDesc c264_desc;
extern const CoreDesc zxnext_desc;
extern const CoreDesc neogeo_desc;
#endif
