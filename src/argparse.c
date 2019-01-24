/* Copyright (c) 2010-2018 the corto developers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <corto.util.argparse>

static void corto_arginit(corto_argdata *data) {
    corto_argdata *pattern = NULL;
    corto_int32 p = 0;

    while (data[p].pattern) {
        pattern = &data[p];
        if (pattern->match) {
            *(pattern->match) = NULL;
        }
        if (pattern->args) {
            *(pattern->args) = NULL;
        }
        p++;
    }
    data->gc = NULL;
}

corto_argdata* corto_argparse(char *argv[], corto_argdata *data) {
    char *arg;
    corto_int32 a = 0;

    corto_arginit(data);

    if (!argv) {
        return data;
    }

    while ((arg = argv[a])) {
        corto_int32 p = 0;
        corto_bool match = FALSE;
        corto_argdata *pattern = NULL, *tentative = NULL;
        corto_int32 count = 0;

        /* Skip empty arguments */
        if (!strlen(argv[a])) {
            a++;
            continue;
        }

        /* Match argument against patterns */
        while (!match && data[p].pattern) {
            pattern = &data[p];

            /* If arguments are required and there are none, there's no match */
            if ((argv[a + 1] && argv[a + 1][0] != '-') || !pattern->args) {

                /* '$' Indicates constraint */
                if (pattern->pattern[0] == '$') {
                    corto_int32 matchCount = pattern->match ?
                        *pattern->match ? ut_ll_count(*pattern->match) : 0 : 0;
                    corto_int32 argCount = pattern->args ?
                        *pattern->args ? ut_ll_count(*pattern->args) : 0 : 0;

                    /* | can be used with + or ? to indicate an OR relationship
                     * between multiple AT LEAST/AT MOST once patterns */
                    if (pattern->pattern[1] == '|') {
                        count += argCount + matchCount;
                    } else {
                        count = argCount + matchCount;
                    }

                    /* Match AT MOST once */
                    if (pattern->pattern[1] == '?') {
                        if (!count && !fnmatch(pattern->pattern + 2, arg, 0)) {
                            tentative = pattern;
                            /* Optionals will match when all other constraints
                             * are satisfied */
                        }

                    /* Match AT LEAST once */
                    } else if ((pattern->pattern[1] == '+') || (pattern->pattern[1] == '|')) {
                        if (!fnmatch(pattern->pattern + 2, arg, 0)) {
                            if (count && tentative && !fnmatch(tentative->pattern + 2, arg, 0))  {
                                /* Push back one element to optional pattern */
                                if (matchCount && pattern->match && *pattern->match) {
                                    corto_string arg = ut_ll_takeFirst(*pattern->match);
                                    *tentative->match = ut_ll_new();
                                    ut_ll_append(*tentative->match, arg);
                                }
                                if (argCount && pattern->args && *pattern->args) {
                                    corto_string arg = ut_ll_takeFirst(*pattern->args);
                                    *tentative->args = ut_ll_new();
                                    ut_ll_append(*tentative->args, arg);
                                }
                                tentative = NULL;
                                match = TRUE;
                            } else {
                                tentative = NULL;
                                match = TRUE;
                            }
                        }

                    /* Match an argument on a fixed position */
                    } else {
                        int num = atoi(&pattern->pattern[1]);
                        if (num == a) {
                            match = TRUE;
                        }
                    }
                } else {
                    if ((arg[0] != '-') || (pattern->pattern[0] == '-')) {
                        if (!fnmatch(pattern->pattern, arg, 0)) {
                            match = TRUE;
                        }
                    }
                }
            }
            p++;
        }

        if (tentative && !fnmatch(tentative->pattern + 2, arg, 0)) {
            pattern = tentative;
            match = TRUE;
        }

        if (match) {
            if (pattern->match) {
                 if(!*(pattern->match)) {
                    *(pattern->match) = ut_ll_new();
                }
                ut_ll_append(*(pattern->match), arg);
            }

            if (pattern->args) {
                if (argv[a + 1]) {
                    if (!*pattern->args) {
                        *(pattern->args) = ut_ll_new();
                    }
                    a++;
                    char *arg = argv[a], *ptr = arg, *prev;
                    do {
                        prev = ptr;
                        ptr = strchr(ptr + 1, ',');
                        if (ptr) {
                            int len = ptr - prev;
                            prev = strdup(prev);
                            if (!pattern->gc) {
                                pattern->gc = ut_ll_new();
                            }
                            ut_ll_append(pattern->gc, prev);
                            prev[len] = '\0';
                        }
                        ut_ll_append(*(pattern->args), prev);
                    } while (ptr && ++ptr);
                } else {
                    ut_throw("missing argument for option %s", arg);
                    goto error;
                }
            }
        } else {
            ut_throw("unknown option '%s'", arg);
            goto error;
        }
        a++;
    }

    return data;
error:
    return NULL;
}

static corto_bool corto_argcleanDeleted(
  ut_ll list,
  ut_ll *deleted,
  corto_int32 count)
{
    corto_int32 i;

    for (i = 0; i < count; i ++) {
        if (deleted[i] == list) {
            return TRUE;
        }
    }

    return FALSE;
}

void corto_argclean(corto_argdata *data) {
    corto_argdata *pattern = NULL;
    corto_int32 p = 0;
    ut_ll deleted[CORTO_ARG_MAX * 2];
    corto_uint32 count = 0;

    while (data[p].pattern) {
        pattern = &data[p];
        if (pattern->match && *pattern->match) {
            if (!corto_argcleanDeleted(*pattern->match, deleted, count)) {
                ut_ll_free(*pattern->match);
                deleted[count ++] = *pattern->match;
            }
        }
        if (pattern->args && *pattern->args) {
            if (!corto_argcleanDeleted(*pattern->args, deleted, count)) {
                ut_ll_free(*pattern->args);
                deleted[count ++] = *pattern->args;
            }
        }
        if (data[p].gc) {
          ut_iter it = ut_ll_iter(data[p].gc);
          while (ut_iter_hasNext(&it)) {
            corto_string s = ut_iter_next(&it);
            corto_dealloc(s);
          }
          ut_ll_free(data[p].gc);
        }
        p++;
    }
}

int cortomain(int argc, char *argv[]) {

    return 0;
}
