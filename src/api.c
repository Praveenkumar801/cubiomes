#include "api.h"
#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../biomes.h"
#include "../finders.h"

/* ── tiny JSON helpers (no external library) ────────────────────────────── */

/*
 * Advance *p past whitespace and an optional ':' separator.
 */
static const char *skip_ws_colon(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p == ':') { p++; }
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

/*
 * Find the value associated with JSON *key* starting from *json*.
 * Returns a pointer to the first character of the value, or NULL.
 */
static const char *json_find_value(const char *json, const char *key)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p)
        return NULL;
    p += strlen(needle);
    return skip_ws_colon(p);
}

/*
 * Read a JSON string value into *out* (null-terminated, at most maxlen chars
 * including NUL).  Returns 1 on success, 0 on failure.
 */
static int json_read_string(const char *json, const char *key,
                            char *out, int maxlen)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '"')
        return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < maxlen - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return 1;
}

/*
 * Read a JSON integer (int64) value.  Returns 1 on success, 0 on failure.
 */
static int json_read_int64(const char *json, const char *key, int64_t *out)
{
    const char *p = json_find_value(json, key);
    if (!p)
        return 0;
    char *end;
    *out = (int64_t)strtoll(p, &end, 10);
    return end != p;
}

/*
 * Read a JSON integer (int) value.
 */
static int json_read_int(const char *json, const char *key, int *out)
{
    int64_t v;
    if (!json_read_int64(json, key, &v))
        return 0;
    *out = (int)v;
    return 1;
}

/* ── request parsing ─────────────────────────────────────────────────────── */

/*
 * Parse the POST body into a SearchRequest.
 * Returns 1 on success, 0 on validation failure (sets *errmsg).
 */
static int parse_request(const char *body, SearchRequest *req,
                         const char **errmsg)
{
    memset(req, 0, sizeof(*req));

    /* version */
    char version_str[32] = {0};
    if (!json_read_string(body, "version", version_str, sizeof(version_str))) {
        *errmsg = "missing version";
        return 0;
    }
    req->mc_version = parse_mc_version(version_str);
    if (req->mc_version == MC_UNDEF) {
        *errmsg = "unknown version string";
        return 0;
    }

    /* seed_start / seed_end */
    if (!json_read_int64(body, "seed_start", &req->seed_start)) {
        *errmsg = "missing seed_start";
        return 0;
    }
    if (!json_read_int64(body, "seed_end", &req->seed_end)) {
        *errmsg = "missing seed_end";
        return 0;
    }
    if (req->seed_end < req->seed_start) {
        *errmsg = "seed_end must be >= seed_start";
        return 0;
    }
    if (req->seed_end - req->seed_start > 1000000000LL) {
        *errmsg = "seed range must not exceed 1 billion";
        return 0;
    }

    /* max_results */
    if (!json_read_int(body, "max_results", &req->max_results) ||
        req->max_results <= 0) {
        *errmsg = "missing or invalid max_results";
        return 0;
    }
    if (req->max_results > MAX_RESULTS)
        req->max_results = MAX_RESULTS;

    /* structures array */
    const char *arr = strstr(body, "\"structures\"");
    if (!arr) {
        *errmsg = "missing structures";
        return 0;
    }
    arr = strchr(arr, '[');
    if (!arr) {
        *errmsg = "structures is not an array";
        return 0;
    }
    arr++; /* skip '[' */

    req->num_structures = 0;
    while (*arr && req->num_structures < MAX_STRUCT_QUERIES) {
        /* find next '{' */
        const char *obj = strchr(arr, '{');
        if (!obj)
            break;

        /* find matching '}' */
        const char *end = strchr(obj, '}');
        if (!end)
            break;

        /* stop if the '{' comes after the closing ']' of the array */
        const char *close_arr = strchr(arr, ']');
        if (close_arr && obj > close_arr)
            break;

        /* copy this object into a small buffer for parsing */
        char buf[256];
        size_t len = (size_t)(end - obj) + 1;
        if (len >= sizeof(buf))
            len = sizeof(buf) - 1;
        memcpy(buf, obj, len);
        buf[len] = '\0';

        char type_name[64] = {0};
        int64_t max_dist = 0;

        json_read_string(buf, "type", type_name, sizeof(type_name));
        json_read_int64(buf, "max_distance", &max_dist);

        int stype = parse_structure_type(type_name);
        if (stype < 0) {
            *errmsg = "unknown structure type";
            return 0;
        }
        if (max_dist <= 0) {
            *errmsg = "max_distance must be positive";
            return 0;
        }

        /* Validate that this structure type is supported in the requested version */
        StructureConfig sconf;
        if (!getStructureConfig(stype, req->mc_version, &sconf)) {
            *errmsg = "structure type not available in requested version";
            return 0;
        }

        req->structures[req->num_structures].type         = stype;
        req->structures[req->num_structures].max_distance = (int)max_dist;
        req->num_structures++;

        arr = end + 1;
    }

    if (req->num_structures == 0) {
        *errmsg = "structures array is empty";
        return 0;
    }

    return 1;
}

/* ── response formatting ─────────────────────────────────────────────────── */

/* Max bytes per seed entry in the JSON array: up to 20 digits + comma */
#define JSON_SEED_ENTRY_MAX 22

/*
 * Write the JSON response into a heap-allocated string.
 * Caller must free() it.
 */
static char *format_response(const SearchResult *result)
{
    /* Estimate size: fixed overhead + per-seed entries + scanned field */
    size_t cap = 64 + (size_t)result->count * JSON_SEED_ENTRY_MAX;
    char  *buf = (char *)malloc(cap);
    if (!buf)
        return NULL;

    int pos = 0;
    pos += snprintf(buf + pos, cap - (size_t)pos, "{\"seeds\":[");
    for (int i = 0; i < result->count; i++) {
        if (i > 0)
            pos += snprintf(buf + pos, cap - (size_t)pos, ",");
        pos += snprintf(buf + pos, cap - (size_t)pos,
                        "%lld", (long long)result->seeds[i]);
    }
    pos += snprintf(buf + pos, cap - (size_t)pos,
                    "],\"scanned\":%lld}", (long long)result->scanned);
    return buf;
}

/* ── libmicrohttpd connection state ─────────────────────────────────────── */

typedef struct {
    char  *data;
    size_t size;
} PostBuffer;

/* ── HTTP handler ────────────────────────────────────────────────────────── */

static enum MHD_Result send_response(struct MHD_Connection *conn,
                                     unsigned int           status,
                                     const char            *body)
{
    struct MHD_Response *resp =
        MHD_create_response_from_buffer(strlen(body),
                                        (void *)body,
                                        MHD_RESPMEM_MUST_COPY);
    if (!resp)
        return MHD_NO;
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(conn, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

enum MHD_Result handle_request(void *cls,
                                struct MHD_Connection *connection,
                                const char            *url,
                                const char            *method,
                                const char            *version,
                                const char            *upload_data,
                                size_t                *upload_data_size,
                                void                 **con_cls)
{
    (void)cls; (void)version;

    /* Only POST /search is supported */
    if (strcmp(url, "/search") != 0) {
        return send_response(connection, MHD_HTTP_NOT_FOUND,
                             "{\"error\":\"not found\"}");
    }
    if (strcmp(method, "POST") != 0) {
        return send_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                             "{\"error\":\"use POST\"}");
    }

    /* First call: allocate per-connection accumulation buffer */
    if (*con_cls == NULL) {
        PostBuffer *pb = (PostBuffer *)calloc(1, sizeof(PostBuffer));
        if (!pb)
            return MHD_NO;
        *con_cls = pb;
        return MHD_YES;
    }

    PostBuffer *pb = (PostBuffer *)(*con_cls);

    /* Accumulate body chunks */
    if (*upload_data_size > 0) {
        char *tmp = (char *)realloc(pb->data,
                                    pb->size + *upload_data_size + 1);
        if (!tmp)
            return MHD_NO;
        pb->data = tmp;
        memcpy(pb->data + pb->size, upload_data, *upload_data_size);
        pb->size += *upload_data_size;
        pb->data[pb->size] = '\0';
        *upload_data_size = 0;
        return MHD_YES;
    }

    /* Body fully received: parse and run the search */
    if (!pb->data || pb->size == 0) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"empty body\"}");
    }

    SearchRequest req;
    const char   *errmsg = NULL;

    if (!parse_request(pb->data, &req, &errmsg)) {
        char errbuf[256];
        snprintf(errbuf, sizeof(errbuf), "{\"error\":\"%s\"}",
                 errmsg ? errmsg : "bad request");
        return send_response(connection, MHD_HTTP_BAD_REQUEST, errbuf);
    }

    SearchResult result;
    memset(&result, 0, sizeof(result));
    search_seeds(&req, &result);

    char *resp_body = format_response(&result);
    if (!resp_body) {
        return send_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                             "{\"error\":\"out of memory\"}");
    }

    enum MHD_Result ret =
        send_response(connection, MHD_HTTP_OK, resp_body);
    free(resp_body);
    return ret;
}

void cleanup_request(void *cls, struct MHD_Connection *connection,
                     void **con_cls, enum MHD_RequestTerminationCode toe)
{
    (void)cls; (void)connection; (void)toe;
    PostBuffer *pb = (PostBuffer *)(*con_cls);
    if (pb) {
        free(pb->data);
        free(pb);
        *con_cls = NULL;
    }
}
