#include "mini_json.h"

#include <string.h>

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    return p;
}

/* Parse a JSON string starting at the opening quote. */
static const char *parse_string(const char *p, char *out, size_t out_size, bool *too_long)
{
    size_t written = 0;
    bool truncated = false;

    if (*p != '"') {
        return NULL;
    }
    ++p;
    while (*p != '\0' && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            char esc = *p++;
            switch (esc) {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case '/':  c = '/';  break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            case 'u':  /* \uXXXX: keep a '?' and skip four hex digits */
                for (uint8_t i = 0; i < 4u && *p != '\0'; ++i) {
                    ++p;
                }
                c = '?';
                break;
            default:
                c = esc;
                break;
            }
        }
        if (out != NULL && written + 1u < out_size) {
            out[written++] = c;
        } else {
            truncated = true;
        }
    }
    if (*p != '"') {
        return NULL;
    }
    p++;
    if (out != NULL && out_size > 0u) {
        out[written < out_size ? written : out_size - 1u] = '\0';
    }
    if (too_long != NULL) {
        *too_long = truncated;
    }
    return p;
}

static const char *skip_value(const char *p)
{
    p = skip_ws(p);
    if (*p == '"') {
        char dummy;
        bool long_flag = false;
        return parse_string(p, &dummy, 1u, &long_flag);
    }
    if (*p == '{' || *p == '[') {
        char open = *p;
        char close = (open == '{') ? '}' : ']';
        int depth = 0;
        bool in_string = false;
        while (*p != '\0') {
            char c = *p++;
            if (in_string) {
                if (c == '\\') {
                    if (*p != '\0') {
                        ++p;
                    }
                } else if (c == '"') {
                    in_string = false;
                }
            } else if (c == '"') {
                in_string = true;
            } else if (c == open) {
                ++depth;
            } else if (c == close) {
                --depth;
                if (depth == 0) {
                    return p;
                }
            }
        }
        return NULL;
    }
    while (*p != '\0' && *p != ',' && *p != '}') {
        ++p;
    }
    return p;
}

typedef struct {
    const char *key;
    char *str_out;
    size_t str_size;
    uint32_t *u32_out;
    int32_t *i32_out;
    bool *bool_out;
    bool found;
    bool invalid;
} json_query_t;

static void store_value(json_query_t *q, const char *p)
{
    p = skip_ws(p);
    if (q->str_out != NULL) {
        bool too_long = false;
        const char *end = parse_string(p, q->str_out, q->str_size, &too_long);
        if (end == NULL) {
            q->invalid = true;
        } else if (too_long) {
            q->invalid = true;
        } else {
            q->found = true;
        }
        return;
    }

    if (q->bool_out != NULL) {
        if (strncmp(p, "true", 4u) == 0) {
            *q->bool_out = true;
            q->found = true;
        } else if (strncmp(p, "false", 5u) == 0) {
            *q->bool_out = false;
            q->found = true;
        } else {
            q->invalid = true;
        }
        return;
    }

    bool negative = false;
    if (*p == '-') {
        negative = true;
        ++p;
    }
    if (*p < '0' || *p > '9') {
        q->invalid = true;
        return;
    }
    uint32_t value = 0u;
    while (*p >= '0' && *p <= '9') {
        uint32_t digit = (uint32_t)(*p - '0');
        if (value > (0xFFFFFFFFu - digit) / 10u) {
            q->invalid = true;
            return;
        }
        value = value * 10u + digit;
        ++p;
    }
    if (q->u32_out != NULL) {
        if (negative) {
            q->invalid = true;
            return;
        }
        *q->u32_out = value;
    } else if (q->i32_out != NULL) {
        if (negative) {
            if (value > 0x80000000u) {
                q->invalid = true;
                return;
            }
            *q->i32_out = -(int32_t)value;
        } else {
            if (value > 0x7FFFFFFFu) {
                q->invalid = true;
                return;
            }
            *q->i32_out = (int32_t)value;
        }
    }
    q->found = true;
}

static int query(const char *json, json_query_t *q)
{
    if (json == NULL || q->key == NULL) {
        return JSON_INVALID;
    }
    const char *p = skip_ws(json);
    if (*p != '{') {
        return JSON_INVALID;
    }
    ++p;

    for (;;) {
        p = skip_ws(p);
        if (*p == '}') {
            return q->found ? JSON_OK : (q->invalid ? JSON_INVALID : JSON_NOT_FOUND);
        }
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p != '"') {
            return JSON_INVALID;
        }
        char key[40];
        bool key_too_long = false;
        p = parse_string(p, key, sizeof(key), &key_too_long);
        if (p == NULL || key_too_long) {
            return JSON_INVALID;
        }
        p = skip_ws(p);
        if (*p != ':') {
            return JSON_INVALID;
        }
        ++p;
        p = skip_ws(p);

        if (strcmp(key, q->key) == 0) {
            store_value(q, p);
            if (q->invalid) {
                return JSON_INVALID;
            }
            /* Continue to validate the rest of the document lightly. */
        }
        const char *next = skip_value(p);
        if (next == NULL) {
            return JSON_INVALID;
        }
        p = next;
    }
}

int json_get_string(const char *json, const char *key, char *out, size_t out_size)
{
    json_query_t q = {0};
    q.key = key;
    q.str_out = out;
    q.str_size = out_size;
    return query(json, &q);
}

int json_get_u32(const char *json, const char *key, uint32_t *out)
{
    json_query_t q = {0};
    q.key = key;
    q.u32_out = out;
    return query(json, &q);
}

int json_get_i32(const char *json, const char *key, int32_t *out)
{
    json_query_t q = {0};
    q.key = key;
    q.i32_out = out;
    return query(json, &q);
}

int json_get_bool(const char *json, const char *key, bool *out)
{
    json_query_t q = {0};
    q.key = key;
    q.bool_out = out;
    return query(json, &q);
}

int json_escape_string(char *out, size_t out_size, const char *input)
{
    size_t written = 0u;
    if (out == NULL || out_size == 0u || input == NULL) {
        return -1;
    }
    for (const char *p = input; *p != '\0'; ++p) {
        const char *esc = NULL;
        switch (*p) {
        case '"':  esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\n': esc = "\\n";  break;
        case '\r': esc = "\\r";  break;
        case '\t': esc = "\\t";  break;
        default: break;
        }
        if (esc != NULL) {
            size_t len = strlen(esc);
            if (written + len + 1u > out_size) {
                return -1;
            }
            memcpy(out + written, esc, len);
            written += len;
        } else {
            if (written + 2u > out_size) {
                return -1;
            }
            out[written++] = *p;
        }
    }
    out[written] = '\0';
    return 0;
}
