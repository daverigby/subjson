/* This file does the brunt of iterating through a buffer and determining the
 * offset where a given path may begin or end.. */

#define INCLUDE_JSONSL_SRC
#include "jsonsl_header.h"
#include "subdoc-api.h"
#include "match.h"

using namespace Subdoc;

typedef struct {
    const char *curhk;
    jsonsl_jpr_t jpr;
    size_t hklen;
    Match *match;
} parse_ctx;

static void push_callback(jsonsl_t jsn,jsonsl_action_t, struct jsonsl_state_st *, const jsonsl_char_t *);
static void pop_callback(jsonsl_t jsn,jsonsl_action_t, struct jsonsl_state_st *, const jsonsl_char_t *);

static parse_ctx *get_ctx(const jsonsl_t jsn)
{
    return (parse_ctx *)jsn->data;
}

static int
err_callback(jsonsl_t jsn, jsonsl_error_t err, struct jsonsl_state_st *state,
    jsonsl_char_t *at)
{
    parse_ctx *ctx = get_ctx(jsn);
    ctx->match->status = err;
    (void)state; (void)at;
    return 0;
}

// Updates the state to reflect information on the parent. This is used by
// the subdoc code for insertion/deletion operations.
static void
update_possible(parse_ctx *ctx, const struct jsonsl_state_st *state, const char *at)
{
    ctx->match->loc_parent.at = at;
    ctx->match->match_level = state->level;
}

static void
unique_callback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *st,
    const jsonsl_char_t *at)
{
    parse_ctx *ctx = get_ctx(jsn);
    Match *m = ctx->match;
    int rv;
    size_t slen;

    if (action == JSONSL_ACTION_PUSH) {
        /* abs. beginning of token */
        ctx->curhk = at;
        return;
    }

    if (st->level == jsn->max_callback_level-2) {
        /* Popping the parent state! */
        jsn->action_callback_POP = pop_callback;
        jsn->action_callback_PUSH = push_callback;
        pop_callback(jsn, action, st, at);
        return;
    }

    slen = st->pos_cur - st->pos_begin;

    if (st->type == JSONSL_T_STRING) {
        slen++;

        if (m->ensure_unique.length < 2) {
            /* not a string */
            return;
        } else if (slen != m->ensure_unique.length) {
            return; /* Length mismatch */
        }

        rv = strncmp(ctx->curhk + 1, m->ensure_unique.at + 1, slen-2);

    } else if (st->type == JSONSL_T_SPECIAL) {
        if (m->ensure_unique.length != slen) {
            return;
        } else if (slen != m->ensure_unique.length) {
            return;
        }

        rv = strncmp(ctx->curhk, m->ensure_unique.at, slen);

    } else {
        /* We can't reliably indicate uniqueness for non-primitives */
        m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
        rv = 0;
    }

    if (rv == 0) {
        m->unique_item_found = 1;
        jsn->max_callback_level = 1;
    }
}

/* Make code a bit more readable */
#define M_POSSIBLE JSONSL_MATCH_POSSIBLE
#define M_NOMATCH JSONSL_MATCH_NOMATCH
#define M_COMPLETE JSONSL_MATCH_COMPLETE
#define IS_CONTAINER JSONSL_STATE_IS_CONTAINER

static void
push_callback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *st,
    const jsonsl_char_t *at)
{
    parse_ctx *ctx = get_ctx(jsn);
    Match *m = ctx->match;
    struct jsonsl_state_st *parent = jsonsl_last_state(jsn, st);
    unsigned prtype = parent->type;

    if (st->type == JSONSL_T_HKEY) {
        ctx->curhk = at+1;
        return;
    }

    // If the parent is a match candidate
    if (parent->mres == M_POSSIBLE) {
        // Key is either the array offset or the dictionary key.
        size_t nkey = (prtype == JSONSL_T_OBJECT) ? ctx->hklen : parent->nelem - 1;

        /* Run the match */
        st->mres = jsonsl_jpr_match(ctx->jpr, prtype, parent->level, ctx->curhk, nkey);

        // jsonsl's jpr is a bit laxer here. but in our case
        // it's impossible for a primitive to result in a "possible" result,
        // since a partial result should always mean a container
        if (st->mres == M_POSSIBLE && IS_CONTAINER(st) == 0) {
            st->mres = JSONSL_MATCH_TYPE_MISMATCH;
        }

        if (st->mres == JSONSL_MATCH_COMPLETE) {
            m->matchres = JSONSL_MATCH_COMPLETE;

            m->loc_match.at = at;
            m->match_level = st->level;
            m->type = st->type;

            if (prtype == JSONSL_T_OBJECT) {
                m->has_key = true;

                // The beginning of the key (for "subdoc" purposes) actually
                // _includes_ the opening and closing quotes
                m->loc_key.at = ctx->curhk - 1;
                m->loc_key.length = ctx->hklen+2;

                // I'm not sure if it's used.
                m->position = (parent->nelem - 1) / 2;
            } else {

                // Array doesn't have a key
                m->has_key = 0;
                // array[n]
                m->position = parent->nelem - 1;
            }

            if (m->ensure_unique.at) {
                if (prtype != JSONSL_T_LIST) {
                    // Can't check "uniquness" in an array!
                    m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
                    st->ignore_callback = 1;
                } else {
                    jsn->action_callback_POP = unique_callback;
                    jsn->action_callback_PUSH = unique_callback;
                    unique_callback(jsn, action, st, at);
                }
            }

        } else if (st->mres == JSONSL_MATCH_NOMATCH) {
            // Can't have a match on this tree. Ignore subsequent callbacks here.
            st->ignore_callback = 1;

        } else if (st->mres == JSONSL_MATCH_POSSIBLE) {
            // Update our depth thus far
            update_possible(ctx, st, at);
        } else if (st->mres == JSONSL_MATCH_TYPE_MISMATCH) {
            st->ignore_callback = 1;
            m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
        }
    }
    (void)action; /* always push */
}

/* This callback serves three purposes:
 * 1. It captures the hash key
 * 2. It captures the length of the match
 * 3. It captures the length of the deepest parent
 */
static void
pop_callback(jsonsl_t jsn, jsonsl_action_t, struct jsonsl_state_st *state,
    const jsonsl_char_t *)
{
    parse_ctx *ctx = get_ctx(jsn);
    Match *m = ctx->match;
    size_t end_pos = jsn->pos;

    if (state->type == JSONSL_T_HKEY) {
        // All we care about is recording the length of the key. We'll use
        // this later on when matching (in the PUSH callback of a new element)
        ctx->hklen = state->pos_cur - (state->pos_begin + 1);
        return;
    }

    if (state->mres == JSONSL_MATCH_COMPLETE) {
        // This is the matched element. Record the end location of the
        // match.
        m->loc_match.length = end_pos - state->pos_begin;
        if (state->type != JSONSL_T_SPECIAL) {
            m->loc_match.length++; /* Include the terminating token */
        } else {
            m->sflags = state->special_flags;
            m->numval = state->nelem;
        }

        // Don't care about more data
        jsn->max_callback_level = state->level;
        return;
    }

    // Not a container and not a full match. We can't do anything here.
    if (!JSONSL_STATE_IS_CONTAINER(state)) {
        return;
    }

    // We already recorded the deepest-most parent.
    if (m->loc_parent.length) {
        return;
    }

    // This isn't part of the match tree, so we just ignore it
    if (state->mres != JSONSL_MATCH_POSSIBLE) {
        return;
    }

    // If we haven't bailed out yet, it means this is the deepest most parent
    // match. This can either be the actual parent of the match (if a match
    // is found), or the deepest existing parent which exists in the document.
    // The latter information is needed for MKDIR_P semantics.

    // Record the length of the parent
    m->loc_parent.length = end_pos - state->pos_begin;
    m->loc_parent.length++;

    // Record the number of "siblings" in the container. This is used by
    // insertion/removal operations to determine comma placement, if any,
    // before or after the current match.
    if (state->type == JSONSL_T_OBJECT) {
        m->num_siblings = state->nelem / 2;
    } else {
        m->num_siblings = state->nelem;

        // For array-based APPEND operations we need the position of the
        // last child (which may not exist if the base array is empty).
        if (m->num_siblings && m->get_last_child_pos) {
            /* Set the last child begin position */
            const struct jsonsl_state_st *child = jsonsl_last_child(jsn, state);

            // Special semantics for this field when using get_last_child_pos.
            // See field documentation in header.
            m->loc_key.length = child->pos_begin;
            m->loc_key.at = NULL;

            // Set information about the last child itself.
            m->type = child->type;
            m->sflags = child->special_flags;
            m->numval = child->nelem;
        }
    }
    if (m->matchres == JSONSL_MATCH_COMPLETE) {
        /* Exclude ourselves from the 'sibling' count */
        m->num_siblings--;
    }

    // If the match was not found, we need to do some additional sanity
    // checking (usually done inside the push/pop callback in the case
    // where a match _is_ found) to verify our would-be path can actually
    // make sense.
    if (m->matchres != JSONSL_MATCH_COMPLETE) {
        m->type = state->type;

        struct jsonsl_jpr_component_st *next_comp =
                &ctx->jpr->components[state->level];
        if (next_comp->is_arridx) {
            if (state->type != JSONSL_T_LIST) {
                /* printf("Next component expected list, but we are object\n"); */
                m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
            }
        } else {
            if (state->type != JSONSL_T_OBJECT) {
                /* printf("Next component expected object key, but we are list!\n"); */
                m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
            }
        }
    }

    jsn->max_callback_level = 1;
    jsonsl_stop(jsn);

    // Is this the actual parent of the match?
    if (state->level == ctx->jpr->ncomponents-1) {
        m->immediate_parent_found = 1;
    }
}

/* Called when */
static void initial_callback(jsonsl_t jsn, jsonsl_action_t action,
    struct jsonsl_state_st *state, const jsonsl_char_t *at)
{
    parse_ctx *ctx = get_ctx(jsn);
    /* state is the parent */
    state->mres = jsonsl_jpr_match(ctx->jpr, JSONSL_T_UNKNOWN, 0, NULL, 0);
    jsn->action_callback_PUSH = push_callback;

    if (state->mres == JSONSL_MATCH_POSSIBLE) {
        update_possible(ctx, state, at);
        ctx->match->matchres = JSONSL_MATCH_POSSIBLE;
    } else if (state->mres == JSONSL_MATCH_COMPLETE) {
        /* Match the root element. Simple */
        ctx->match->match_level = state->level;
        ctx->match->has_key = 0;
        ctx->match->loc_match.at = at;
        ctx->match->loc_parent.at = at;
        ctx->match->matchres = JSONSL_MATCH_COMPLETE;
    } else {
        state->ignore_callback = 1;
    }

    (void)action; /* always push */
}

int
Match::exec_match_simple(const char *value, size_t nvalue,
    const Path::CompInfo *jpr, jsonsl_t jsn)
{
    parse_ctx ctx = { NULL };

    ctx.match = this;
    ctx.jpr = (jsonsl_jpr_t)jpr;
    status = JSONSL_ERROR_SUCCESS;

    jsonsl_enable_all_callbacks(jsn);
    jsn->action_callback_PUSH = initial_callback;
    jsn->action_callback_POP = pop_callback;
    jsn->error_callback = err_callback;
    jsn->max_callback_level = ctx.jpr->ncomponents + 1;
    jsn->data = &ctx;

    jsonsl_feed(jsn, value, nvalue);
    jsonsl_reset(jsn);
    return 0;
}

int
Match::exec_match_negix(const char *value, size_t nvalue, const Path *pth,
    jsonsl_t jsn)
{
    /* First component to scan in next iteration */
    size_t cur_start = 1;

    /* For levels, compensate this much for the match level */
    size_t level_offset = 0;

    const jsonsl_jpr_t orig_jpr = (const jsonsl_jpr_t)&pth->jpr_base;
    struct jsonsl_jpr_component_st comp_s[COMPONENTS_ALLOC];

    /* Last length of the subdocument */
    size_t last_len = nvalue;
    /* Pointer to the beginning of the current subdocument */
    const char *last_start = value;

    memcpy(comp_s, orig_jpr->components, sizeof(comp_s[0]) * orig_jpr->ncomponents);

    while (cur_start < orig_jpr->ncomponents) {
        size_t ii;
        int rv, is_last_neg = 0;
        struct jsonsl_jpr_st tmp_jpr = { NULL };

        /* If the last match was not a list or an object,
         * but we still need to descend, then throw an error now. */
        if (last_start != value && type != JSONSL_T_LIST && type != JSONSL_T_OBJECT) {
            matchres = JSONSL_MATCH_TYPE_MISMATCH;
            return 0;
        }

        for (ii = cur_start; ii < orig_jpr->ncomponents; ii++) {
            /* Seek to the next negative index */
            if (orig_jpr->components[ii].is_neg) {
                /* Convert this to the first array index; then switch it
                 * around to extract parent information */
                is_last_neg = 1;
                break;
            }
        }
        /* Assign the new list. Use one before for the ROOT element */
        tmp_jpr.components = &comp_s[cur_start-1];

        /* Adjust for root again */
        tmp_jpr.ncomponents = (ii - cur_start) + 1;
        level_offset += (ii - cur_start);

        tmp_jpr.components[0].ptype = JSONSL_PATH_ROOT;

        /* Clear the match. There's no good way to preserve info here,
         * unfortunately. */
        clear();

        /* Always set this */
        get_last_child_pos = 1;


        /* If we need to find the _last_ item, make use of the get_last_child_pos
         * field. To use this effectively, search for the _first_ element (to
         * guarantee a valid parent). Then, once the match is done, the last
         * item can be derived (see below). */
        if (is_last_neg) {
            struct jsonsl_jpr_component_st *comp;

            comp = &tmp_jpr.components[tmp_jpr.ncomponents++];
            comp->ptype = JSONSL_PATH_NUMERIC;
            comp->is_arridx = 1;
            comp->idx = 0;
        }

        rv = exec_match_simple(last_start, last_len, &tmp_jpr, jsn);
        match_level += level_offset;

        if (rv != 0) { /* error */
            return rv;
        } else if (status != JSONSL_ERROR_SUCCESS) {
            return 0;
        } else if (matchres != JSONSL_MATCH_COMPLETE) {
            return 0;
        }

        /* No errors so far. In this case, set last_start */
        if (is_last_neg) {
            /* Offset into last child position */
            loc_match.at = last_start + loc_key.length;

            /* Set the length. The length is at least as long as the parent */
            loc_match.length = (loc_parent.at + loc_parent.length) - loc_match.at;
            loc_match.length--;

            /* Strip trailing whitespace. Leading whitespace is stripped
             * within jsonsl itself */
            while (loc_match.at[loc_match.length-1] == ' ' && loc_match.length) {
                loc_match.length--;
            }

            /* Set the position */
            position = num_siblings;

            last_start = loc_match.at;
            last_len = loc_match.length;
        }

        /* Chomp off the current component */
        cur_start = ii + 1;
    }

    return 0;
}

/**
 * Retrieves a buffer for the designated _path_ in the JSON document. The
 * actual length of the remaining components may be determined by using
 * pointer arithmetic.
 *
 * @param value The value buffer to search
 * @param nvalue Length of the value buffer
 * @param nj The path component to search for
 * @param[out] a result structure for the path
 * @return 0 if found, otherwise an error code
 */
int
Match::exec_match(const char *value, size_t nvalue, const Path *pth, jsonsl_t jsn)
{
    if (!pth->has_negix) {
        return exec_match_simple(value, nvalue,
            (const jsonsl_jpr_t)&pth->jpr_base, jsn);
    } else {
        return exec_match_negix(value, nvalue, pth, jsn);
    }
}

Match::Match()
{
    clear();
}

Match::~Match()
{
}

void
Match::clear()
{
    // TODO: Memberwise reset
    memset(this, 0, sizeof(*this));
}

jsonsl_t
Match::jsn_alloc()
{
    return jsonsl_new(COMPONENTS_ALLOC);
}

void
Match::jsn_free(jsonsl_t jsn)
{
    jsonsl_destroy(jsn);
}

typedef struct {
    int err;
    int rootcount;
    int flags;
} validate_ctx;

static int
validate_err_callback(jsonsl_t jsn, jsonsl_error_t err,
    struct jsonsl_state_st *state, jsonsl_char_t *at)
{
    validate_ctx *ctx = (validate_ctx *)jsn->data;
    ctx->err = err;
    printf("ERROR: AT=%s\n", at);
    (void)at; (void)state;
    return 0;
}

static void
validate_callback(jsonsl_t jsn, jsonsl_action_t action,
    struct jsonsl_state_st *state, const jsonsl_char_t *at)
{
    validate_ctx *ctx = (validate_ctx *)jsn->data;

    if (state->level == 1) {
        ctx->rootcount++;

    } else if (action == JSONSL_ACTION_PUSH) {
        assert(state->level == 2);
        if (ctx->flags & SUBDOC_VALIDATE_F_PRIMITIVE) {
            if (JSONSL_STATE_IS_CONTAINER(state)) {
                ctx->err = SUBDOC_VALIDATE_ENOTPRIMITIVE;
                jsn->max_callback_level = 1;
            }
        }

        if (ctx->flags & SUBDOC_VALIDATE_F_SINGLE) {
            const struct jsonsl_state_st *parent = jsonsl_last_state(jsn, state);
            int has_multielem = 0;
            if (parent->type == JSONSL_T_LIST && parent->nelem > 1) {
                has_multielem = 1;
            } else if (parent->type == JSONSL_T_OBJECT && parent->nelem > 2) {
                has_multielem = 1;
            }
            if (has_multielem) {
                ctx->err = SUBDOC_VALIDATE_EMULTIELEM;
                jsn->max_callback_level = 1;
            }
        }

    }
    (void)state;(void)at;
}

static const Loc validate_ARRAY_PRE = { "[", 1 };
static const Loc validate_ARRAY_POST = { "]", 1 };
static const Loc validate_DICT_PRE = { "{\"k\":", 5 };
static const Loc validate_DICT_POST = { "}", 1 };
static const Loc validate_NOOP = { NULL, 0 };

jsonsl_error_t
Match::validate(const char *s, size_t n, jsonsl_t jsn, int mode)
{
    int need_free_jsn = 0;
    int type = mode & SUBDOC_VALIDATE_MODEMASK;
    int flags = mode & SUBDOC_VALIDATE_FLAGMASK;
    const Loc *l_pre, *l_post;

    validate_ctx ctx = { 0,0 };
    if (jsn == NULL) {
        jsn = jsonsl_new(COMPONENTS_ALLOC);
        need_free_jsn = 1;
    }

    jsn->action_callback_POP = NULL;
    jsn->action_callback_PUSH = NULL;

    jsn->action_callback = validate_callback;
    jsn->error_callback = validate_err_callback;

    if (flags) {
        ctx.flags = flags;
        jsn->max_callback_level = 3;
    } else {
        jsn->max_callback_level = 2;
    }

    jsn->call_OBJECT = 1;
    jsn->call_LIST = 1;
    jsn->call_STRING = 1;
    jsn->call_SPECIAL = 1;
    jsn->call_HKEY = 0;
    jsn->call_UESCAPE = 0;
    jsn->data = &ctx;

    if (type == SUBDOC_VALIDATE_PARENT_NONE) {
        l_pre = l_post = &validate_NOOP;
    } else if (type == SUBDOC_VALIDATE_PARENT_ARRAY) {
        l_pre = &validate_ARRAY_PRE;
        l_post = &validate_ARRAY_POST;
    } else if (type == SUBDOC_VALIDATE_PARENT_DICT) {
        l_pre = &validate_DICT_PRE;
        l_post = &validate_DICT_POST;
    } else {
        ctx.err = JSONSL_ERROR_GENERIC;
        goto GT_ERR;
    }

    jsonsl_feed(jsn, l_pre->at, l_pre->length);
    jsonsl_feed(jsn, s, n);
    jsonsl_feed(jsn, l_post->at, l_post->length);

    if (ctx.err == JSONSL_ERROR_SUCCESS) {
        if (ctx.rootcount < 2) {
            ctx.err = SUBDOC_VALIDATE_EPARTIAL;
        } else if (ctx.rootcount > 2) {
            ctx.err = SUBDOC_VALIDATE_EMULTIELEM;
        }
    }

    GT_ERR:
    if (need_free_jsn) {
        jsonsl_destroy(jsn);
    } else {
        jsonsl_reset(jsn);
    }
    return (jsonsl_error_t)ctx.err;
}
