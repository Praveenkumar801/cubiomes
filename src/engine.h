#ifndef ENGINE_H_
#define ENGINE_H_

#include <stdint.h>

#define MAX_STRUCT_QUERIES 16
#define MAX_RESULTS        10
#define MAX_THREADS        16

typedef struct {
    int  type;          /* StructureType enum value */
    int  max_distance;  /* max block distance from (0,0) */
} StructureQuery;

typedef struct {
    int            mc_version;
    int64_t        seed_start;
    int64_t        seed_end;
    int            max_results;
    StructureQuery structures[MAX_STRUCT_QUERIES];
    int            num_structures;
} SearchRequest;

typedef struct {
    int64_t  seeds[MAX_RESULTS];
    int      count;
    int64_t  scanned;
} SearchResult;

/*
 * Parse a Minecraft version string (e.g. "1.16.1") into an MCVersion enum
 * value.  Returns MC_UNDEF (0) on failure.
 */
int parse_mc_version(const char *str);

/*
 * Parse a structure-type name (e.g. "village") into a StructureType enum
 * value.  Returns -1 on failure.
 */
int parse_structure_type(const char *name);

/*
 * Run a multithreaded seed search according to *req and write the results
 * into *result (which must be zero-initialised by the caller).
 */
void search_seeds(const SearchRequest *req, SearchResult *result);

/*
 * Returns a NULL-terminated array of all supported structure-type name
 * strings (e.g. "village", "monument", …).  The array is static; do not
 * free or modify it.
 */
const char * const *get_structure_names(void);

#endif /* ENGINE_H_ */
