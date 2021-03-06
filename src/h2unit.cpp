/* https://github.com/lingjf/h2unit */
/* Jeff Ling , lingjf@gmail.com */

#include "h2unit.h"
#include <cctype>
#include <ctime>
#include <csetjmp>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cfloat>
#include <climits>


#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup
#undef strndup
#undef new
#undef delete
#undef inline

using namespace std;


#ifdef _WIN32
#include <windows.h>
static long __milliseconds()
{
  return timeGetTime() / 1000;
}
#define srandom srand
#define random rand
#define strcasecmp _stricmp
#define strdup _strdup

#pragma warning(disable:4267)
#pragma warning(disable:4311)
#pragma warning(disable:4800)

#else
#include <sys/time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
static long __milliseconds()
{
  struct timeval tv;
  struct timezone tz;
  gettimeofday(&tv, &tz);
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
#endif


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
////////////////////////////// list.c////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

static inline void h2unit_list_init(h2unit_list* node)
{
  node->next = node->prev = node;
}

static inline void __h2unit_list_add(h2unit_list *newl, h2unit_list *prev, h2unit_list *next)
{
  next->prev = newl;
  newl->next = next;
  newl->prev = prev;
  prev->next = newl;
}

static inline void h2unit_list_add_head(h2unit_list *newl, h2unit_list *head)
{
  __h2unit_list_add(newl, head, head->next);
}

static inline void h2unit_list_add_tail(h2unit_list *newl, h2unit_list *head)
{
  __h2unit_list_add(newl, head->prev, head);
}

static inline void h2unit_list_del(h2unit_list *node)
{
  node->next->prev = node->prev;
  node->prev->next = node->next;
  node->next = node->prev = node;
}

static inline bool h2unit_list_empty(h2unit_list *head)
{
  return head->next == head;
}

static inline h2unit_list* h2unit_list_get_head(h2unit_list *head)
{
  return h2unit_list_empty(head) ? ((h2unit_list*)0) : head->next;
}

static inline h2unit_list* h2unit_list_get_tail(h2unit_list *head)
{
  return h2unit_list_empty(head) ? ((h2unit_list*)0) : head->prev;
}

#define h2unit_list_entry(ptr, type, field)  \
   ( (type *)((char *)(ptr)-(unsigned long)(&((type *)0)->field)) )

#define h2unit_list_for_each(iter, head)  \
   for( (iter) = (head)->next; (iter) != (head); (iter) = (iter)->next )

#define h2unit_list_for_each_safe(iter, temp, head)  \
   for( (iter) = (head)->next, (temp) = (iter)->next; (iter) != (head); (iter) = (temp), (temp) = (iter)->next )


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
////////////////////////////// wildcard.c ///////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

static int __wildcard_match(char* pattern, char* text)
{
  for (;;) {
    switch (*pattern) {
    case '\0':
      return *text ? 0 : 1;
    case '?':
      if (!*text++) return 0;
      pattern++;
      break;
    case '*': {
      char *t = text;
      while (*t != '\0') t++;
      do {
        if (__wildcard_match(pattern + 1, t)) return 1;
      } while (t-- > text);
      return 0;
    }
    case '[': {
      int negate = 0, found = 0;
      pattern++;
      if (*pattern == '!' || *pattern == '^') {
        negate = 1;
        pattern++;
      }
      for (; *pattern != ']'; pattern++) {
        switch (*pattern) {
        case '\0':
          return 0;
        case '-': {
          char a = *(pattern - 1);
          char z = *(pattern + 1);
          if (a == '[' || a == '!' || a == '^' || a == '-') a = 0x00;
          if (z == ']' || z == '!' || z == '^' || z == '-') z = 0x7f;
          if (a <= *text && *text <= z) found = 1;
          break;
        }
        case '\\':
          pattern++;
        default:
          if (*pattern == *text) found = 1;
          break;
        }
      }
      if ((negate && found) || (!negate && !found)) return 0;
      pattern++;
      text++;
      break;
    }
    case '\\':
      pattern++;
    default:
      if (*pattern++ != *text++) return 0;
      break;
    }
  }
  return 0;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////// slre.c //////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

namespace SLRE
{

struct slre_cap {
  const char *ptr;
  int len;
};


/* Possible flags for slre_match() */
enum { SLRE_IGNORE_CASE = 1 };


/* slre_match() failure codes */
#define SLRE_NO_MATCH               -1
#define SLRE_UNEXPECTED_QUANTIFIER  -2
#define SLRE_UNBALANCED_BRACKETS    -3
#define SLRE_INTERNAL_ERROR         -4
#define SLRE_INVALID_CHARACTER_SET  -5
#define SLRE_INVALID_METACHARACTER  -6
#define SLRE_CAPS_ARRAY_TOO_SMALL   -7
#define SLRE_TOO_MANY_BRANCHES      -8
#define SLRE_TOO_MANY_BRACKETS      -9


#define MAX_BRANCHES 100
#define MAX_BRACKETS 100
#define FAIL_IF(condition, error_code) if (condition) return (error_code)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(ar) (sizeof(ar) / sizeof((ar)[0]))
#endif

#ifdef SLRE_DEBUG
#define DBG(x) printf x
#else
#define DBG(x)
#endif

struct bracket_pair {
  const char *ptr;  /* Points to the first char after '(' in regex  */
  int len;          /* Length of the text between '(' and ')'       */
  int branches;     /* Index in the branches array for this pair    */
  int num_branches; /* Number of '|' in this bracket pair           */
};

struct branch {
  int bracket_index;    /* index for 'struct bracket_pair brackets' */
  /* array defined below                      */
  const char *schlong;  /* points to the '|' character in the regex */
};

struct regex_info {
  /*
   * Describes all bracket pairs in the regular expression.
   * First entry is always present, and grabs the whole regex.
   */
  struct bracket_pair brackets[MAX_BRACKETS];
  int num_brackets;

  /*
   * Describes alternations ('|' operators) in the regular expression.
   * Each branch falls into a specific branch pair.
   */
  struct branch branches[MAX_BRANCHES];
  int num_branches;

  /* Array of captures provided by the user */
  struct slre_cap *caps;
  int num_caps;

  /* E.g. SLRE_IGNORE_CASE. See enum below */
  int flags;
};

static int is_metacharacter(const unsigned char *s)
{
  static const char *metacharacters = "^$().[]*+?|\\Ssdbfnrtv";
  return strchr(metacharacters, *s) != NULL;
}

static int op_len(const char *re)
{
  return re[0] == '\\' && re[1] == 'x' ? 4 : re[0] == '\\' ? 2 : 1;
}

static int set_len(const char *re, int re_len)
{
  int len = 0;

  while (len < re_len && re[len] != ']') {
    len += op_len(re + len);
  }

  return len <= re_len ? len + 1 : -1;
}

static int get_op_len(const char *re, int re_len)
{
  return re[0] == '[' ? set_len(re + 1, re_len - 1) + 1 : op_len(re);
}

static int is_quantifier(const char *re)
{
  return re[0] == '*' || re[0] == '+' || re[0] == '?';
}

static int toi(int x)
{
  return isdigit(x) ? x - '0' : x - 'W';
}

static int hextoi(const unsigned char *s)
{
  return (toi(tolower(s[0])) << 4) | toi(tolower(s[1]));
}

static int match_op(const unsigned char *re, const unsigned char *s, struct regex_info *info)
{
  int result = 0;
  switch (*re) {
  case '\\':
    /* Metacharacters */
    switch (re[1]) {
    case 'S': FAIL_IF(isspace(*s), SLRE_NO_MATCH); result++; break;
    case 's': FAIL_IF(!isspace(*s), SLRE_NO_MATCH); result++; break;
    case 'd': FAIL_IF(!isdigit(*s), SLRE_NO_MATCH); result++; break;
    case 'b': FAIL_IF(*s != '\b', SLRE_NO_MATCH); result++; break;
    case 'f': FAIL_IF(*s != '\f', SLRE_NO_MATCH); result++; break;
    case 'n': FAIL_IF(*s != '\n', SLRE_NO_MATCH); result++; break;
    case 'r': FAIL_IF(*s != '\r', SLRE_NO_MATCH); result++; break;
    case 't': FAIL_IF(*s != '\t', SLRE_NO_MATCH); result++; break;
    case 'v': FAIL_IF(*s != '\v', SLRE_NO_MATCH); result++; break;

    case 'x':
      /* Match byte, \xHH where HH is hexadecimal byte representaion */
      FAIL_IF(hextoi(re + 2) != *s, SLRE_NO_MATCH);
      result++;
      break;

    default:
      /* Valid metacharacter check is done in bar() */
      FAIL_IF(re[1] != s[0], SLRE_NO_MATCH);
      result++;
      break;
    }
    break;

  case '|': FAIL_IF(1, SLRE_INTERNAL_ERROR); break;
  case '$': FAIL_IF(1, SLRE_NO_MATCH); break;
  case '.': result++; break;

  default:
    if (info->flags & SLRE_IGNORE_CASE) {
      FAIL_IF(tolower(*re) != tolower(*s), SLRE_NO_MATCH);
    } else {
      FAIL_IF(*re != *s, SLRE_NO_MATCH);
    }
    result++;
    break;
  }

  return result;
}

static int match_set(const char *re, int re_len, const char *s, struct regex_info *info)
{
  int len = 0, result = -1, invert = re[0] == '^';

  if (invert) re++, re_len--;

  while (len <= re_len && re[len] != ']' && result <= 0) {
    /* Support character range */
    if (re[len] != '-' && re[len + 1] == '-' && re[len + 2] != ']' && re[len + 2] != '\0') {
      result = info->flags &&  SLRE_IGNORE_CASE ? *s >= re[len] && *s <= re[len + 2] : tolower(*s) >= tolower(re[len]) && tolower(*s) <= tolower(re[len + 2]);
      len += 3;
    } else {
      result = match_op((unsigned char *) re + len, (unsigned char *) s, info);
      len += op_len(re + len);
    }
  }
  return (!invert && result > 0) || (invert && result <= 0) ? 1 : -1;
}

static int doh(const char *s, int s_len, struct regex_info *info, int bi);

static int bar(const char *re, int re_len, const char *s, int s_len, struct regex_info *info, int bi)
{
  /* i is offset in re, j is offset in s, bi is brackets index */
  int i, j, n, step;

  for (i = j = 0; i < re_len && j <= s_len; i += step) {

    /* Handle quantifiers. Get the length of the chunk. */
    step = re[i] == '(' ? info->brackets[bi + 1].len + 2 : get_op_len(re + i, re_len - i);

    DBG(("%s [%.*s] [%.*s] re_len=%d step=%d i=%d j=%d\n", __func__, re_len - i, re + i, s_len - j, s + j, re_len, step, i, j));

    FAIL_IF(is_quantifier(&re[i]), SLRE_UNEXPECTED_QUANTIFIER);
    FAIL_IF(step <= 0, SLRE_INVALID_CHARACTER_SET);

    if (i + step < re_len && is_quantifier(re + i + step)) {
      DBG(("QUANTIFIER: [%.*s]%c [%.*s]\n", step, re + i, re[i + step], s_len - j, s + j));
      if (re[i + step] == '?') {
        int result = bar(re + i, step, s + j, s_len - j, info, bi);
        j += result > 0 ? result : 0;
        i++;
      } else if (re[i + step] == '+' || re[i + step] == '*') {
        int j2 = j, nj = j, n1, n2 = -1, ni, non_greedy = 0;

        /* Points to the regexp code after the quantifier */
        ni = i + step + 1;
        if (ni < re_len && re[ni] == '?') {
          non_greedy = 1;
          ni++;
        }

        do {
          if ((n1 = bar(re + i, step, s + j2, s_len - j2, info, bi)) > 0) {
            j2 += n1;
          }
          if (re[i + step] == '+' && n1 < 0) break;

          if (ni >= re_len) {
            /* After quantifier, there is nothing */
            nj = j2;
          } else if ((n2 = bar(re + ni, re_len - ni, s + j2, s_len - j2, info, bi)) >= 0) {
            /* Regex after quantifier matched */
            nj = j2 + n2;
          }
          if (nj > j && non_greedy) break;
        } while (n1 > 0);

        if (n1 < 0 && re[i + step] == '*' && (n2 = bar(re + ni, re_len - ni, s + j, s_len - j, info, bi)) > 0) {
          nj = j + n2;
        }

        DBG(("STAR/PLUS END: %d %d %d %d %d\n", j, nj, re_len - ni, n1, n2));
        FAIL_IF(re[i + step] == '+' && nj == j, SLRE_NO_MATCH);

        /* If while loop body above was not executed for the * quantifier,  */
        /* make sure the rest of the regex matches                          */
        FAIL_IF(nj == j && ni < re_len && n2 < 0, SLRE_NO_MATCH);

        /* Returning here cause we've matched the rest of RE already */
        return nj;
      }
      continue;
    }

    if (re[i] == '[') {
      n = match_set(re + i + 1, re_len - (i + 2), s + j, info);
      DBG(("SET %.*s [%.*s] -> %d\n", step, re + i, s_len - j, s + j, n));
      FAIL_IF(n <= 0, SLRE_NO_MATCH);
      j += n;
    } else if (re[i] == '(') {
      n = SLRE_NO_MATCH;
      bi++;
      FAIL_IF(bi >= info->num_brackets, SLRE_INTERNAL_ERROR);
      DBG(("CAPTURING [%.*s] [%.*s] [%s]\n", step, re + i, s_len - j, s + j, re + i + step));

      if (re_len - (i + step) <= 0) {
        /* Nothing follows brackets */
        n = doh(s + j, s_len - j, info, bi);
      } else {
        int j2;
        for (j2 = 0; j2 <= s_len - j; j2++) {
          if ((n = doh(s + j, s_len - (j + j2), info, bi)) >= 0 && bar(re + i + step, re_len - (i + step), s + j + n, s_len - (j + n), info, bi) >= 0) break;
        }
      }

      DBG(("CAPTURED [%.*s] [%.*s]:%d\n", step, re + i, s_len - j, s + j, n));
      FAIL_IF(n < 0, n);
      if (info->caps != NULL) {
        info->caps[bi - 1].ptr = s + j;
        info->caps[bi - 1].len = n;
      }
      j += n;
    } else if (re[i] == '^') {
      FAIL_IF(j != 0, SLRE_NO_MATCH);
    } else if (re[i] == '$') {
      FAIL_IF(j != s_len, SLRE_NO_MATCH);
    } else {
      FAIL_IF(j >= s_len, SLRE_NO_MATCH);
      n = match_op((unsigned char *) (re + i), (unsigned char *) (s + j), info);
      FAIL_IF(n <= 0, n);
      j += n;
    }
  }

  return j;
}

/* Process branch points */
static int doh(const char *s, int s_len, struct regex_info *info, int bi)
{
  const struct bracket_pair *b = &info->brackets[bi];
  int i = 0, len, result;
  const char *p;

  do {
    p = i == 0 ? b->ptr : info->branches[b->branches + i - 1].schlong + 1;
    len = b->num_branches == 0 ? b->len : i == b->num_branches ? (int) (b->ptr + b->len - p) : (int) (info->branches[b->branches + i].schlong - p);
    DBG(("%s %d %d [%.*s] [%.*s]\n", __func__, bi, i, len, p, s_len, s));
    result = bar(p, len, s, s_len, info, bi);
    DBG(("%s <- %d\n", __func__, result));
  } while (result <= 0 && i++ < b->num_branches);  /* At least 1 iteration */

  return result;
}

static int baz(const char *s, int s_len, struct regex_info *info)
{
  int i, result = -1, is_anchored = info->brackets[0].ptr[0] == '^';

  for (i = 0; i <= s_len; i++) {
    result = doh(s + i, s_len - i, info, 0);
    if (result >= 0) {
      result += i;
      break;
    }
    if (is_anchored) break;
  }

  return result;
}

static void setup_branch_points(struct regex_info *info)
{
  int i, j;
  struct branch tmp;

  /* First, sort branches. Must be stable, no qsort. Use bubble algo. */
  for (i = 0; i < info->num_branches; i++) {
    for (j = i + 1; j < info->num_branches; j++) {
      if (info->branches[i].bracket_index > info->branches[j].bracket_index) {
        tmp = info->branches[i];
        info->branches[i] = info->branches[j];
        info->branches[j] = tmp;
      }
    }
  }

  /*
   * For each bracket, set their branch points. This way, for every bracket
   * (i.e. every chunk of regex) we know all branch points before matching.
   */
  for (i = j = 0; i < info->num_brackets; i++) {
    info->brackets[i].num_branches = 0;
    info->brackets[i].branches = j;
    while (j < info->num_branches && info->branches[j].bracket_index == i) {
      info->brackets[i].num_branches++;
      j++;
    }
  }
}

static int foo(const char *re, int re_len, const char *s, int s_len,
               struct regex_info *info)
{
  int i, step, depth = 0;

  /* First bracket captures everything */
  info->brackets[0].ptr = re;
  info->brackets[0].len = re_len;
  info->num_brackets = 1;

  /* Make a single pass over regex string, memorize brackets and branches */
  for (i = 0; i < re_len; i += step) {
    step = get_op_len(re + i, re_len - i);

    if (re[i] == '|') {
      FAIL_IF(info->num_branches >= (int) ARRAY_SIZE(info->branches), SLRE_TOO_MANY_BRANCHES);
      info->branches[info->num_branches].bracket_index = info->brackets[info->num_brackets - 1].len == -1 ? info->num_brackets - 1 : depth;
      info->branches[info->num_branches].schlong = &re[i];
      info->num_branches++;
    } else if (re[i] == '\\') {
      FAIL_IF(i >= re_len - 1, SLRE_INVALID_METACHARACTER);
      if (re[i + 1] == 'x') {
        /* Hex digit specification must follow */
        FAIL_IF(re[i + 1] == 'x' && i >= re_len - 3, SLRE_INVALID_METACHARACTER);
        FAIL_IF(re[i + 1] ==  'x' && !(isxdigit(re[i + 2]) && isxdigit(re[i + 3])), SLRE_INVALID_METACHARACTER);
      } else {
        FAIL_IF(!is_metacharacter((unsigned char *) re + i + 1), SLRE_INVALID_METACHARACTER);
      }
    } else if (re[i] == '(') {
      FAIL_IF(info->num_brackets >= (int) ARRAY_SIZE(info->brackets), SLRE_TOO_MANY_BRACKETS);
      depth++;  /* Order is important here. Depth increments first. */
      info->brackets[info->num_brackets].ptr = re + i + 1;
      info->brackets[info->num_brackets].len = -1;
      info->num_brackets++;
      FAIL_IF(info->num_caps > 0 && info->num_brackets - 1 > info->num_caps, SLRE_CAPS_ARRAY_TOO_SMALL);
    } else if (re[i] == ')') {
      int ind = info->brackets[info->num_brackets - 1].len == -1 ? info->num_brackets - 1 : depth;
      info->brackets[ind].len = (int) (&re[i] - info->brackets[ind].ptr);
      DBG(("SETTING BRACKET %d [%.*s]\n", ind, info->brackets[ind].len, info->brackets[ind].ptr));
      depth--;
      FAIL_IF(depth < 0, SLRE_UNBALANCED_BRACKETS);
      FAIL_IF(i > 0 && re[i - 1] == '(', SLRE_NO_MATCH);
    }
  }

  FAIL_IF(depth != 0, SLRE_UNBALANCED_BRACKETS);
  setup_branch_points(info);

  return baz(s, s_len, info);
}

int slre_match(const char *regexp, const char *s, int s_len, struct slre_cap *caps, int num_caps, int flags)
{
  struct regex_info info;

  /* Initialize info structure */
  info.flags = flags;
  info.num_brackets = info.num_branches = 0;
  info.num_caps = num_caps;
  info.caps = caps;

  DBG(("========================> [%s] [%.*s]\n", regexp, s_len, s));
  return foo(regexp, (int) strlen(regexp), s, s_len, &info);
}

}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////cJSON.c/////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

namespace cJSON
{

/* cJSON Types: */
#define cJSON_False 0
#define cJSON_True 1
#define cJSON_Null 2
#define cJSON_Number 3
#define cJSON_String 4
#define cJSON_Regexp 5
#define cJSON_Array 6
#define cJSON_Object 7

#define cJSON_IsReference 256

/* The cJSON structure: */
typedef struct cJSON {
  struct cJSON *next, *prev;  /* next/prev allow you to walk array/object chains. Alternatively, use GetArraySize/GetArrayItem/GetObjectItem */
  struct cJSON *child;        /* An array or object item will have a child pointer pointing to a chain of the items in the array/object. */

  int type;                   /* The type of the item, as above. */

  char *valuestring;          /* The item's string, if type==cJSON_String */
  long long valuelong;        /* The item's number, if type==cJSON_Number */
  double valuedouble;         /* The item's number, if type==cJSON_Number */

  char *keystring;            /* The item's name string, if this item is the child of, or is in the list of subitems of an object. */

  int diff;
} cJSON;


static const char *ep;


static cJSON *cJSON_New(void)
{
  cJSON* node = (cJSON*)malloc(sizeof(cJSON));
  if (node) memset(node, 0, sizeof(cJSON));
  return node;
}

static void cJSON_Delete(cJSON *c)
{
  cJSON *next;
  while (c) {
    next = c->next;
    if (!(c->type & cJSON_IsReference) && c->child) cJSON_Delete(c->child);
    if (!(c->type & cJSON_IsReference) && c->valuestring) free(c->valuestring);
    if (c->keystring) free(c->keystring);
    free(c);
    c = next;
  }
}

/* Parse the input text to generate a number, and populate the result into item. */
static const char *parse_number(cJSON *item, const char *num)
{
  double n = 0, sign = 1, scale = 0; int subscale = 0, signsubscale = 1;

  if (*num == '-') sign = -1, num++;  /* Has sign? */
  if (*num == '0') { num++;
    if (*num == 'x' || *num == 'X') { /* Hex? */
      for (num++; (*num >= '0' && *num <= '9') || (*num >= 'A' && *num <= 'F') || (*num >= 'a' && *num <= 'f'); num++) {
        if (*num >= '0' && *num <= '9') n = (n * 16) + (*num) - '0';
        else if (*num >= 'A' && *num <= 'F') n = (n * 16) + 10 + (*num) - 'A';
        else if (*num >= 'a' && *num <= 'f') n = (n * 16) + 10 + (*num) - 'a';
      }
    }
  }
  if (*num >= '1' && *num <= '9') do  n = (n * 10.0) + (*num++ -'0'); while (*num >= '0' && *num <= '9'); /* Number? */
  if (*num == '.' && num[1] >= '0' && num[1] <= '9') {num++;    do  n = (n * 10.0) + (*num++ -'0'), scale--; while (*num >= '0' && *num <= '9');} /* Fractional part? */
  if (*num == 'e' || *num == 'E') { /* Exponent? */
    num++; if (*num == '+') num++;  else if (*num == '-') signsubscale = -1, num++;   /* With sign? */
    while (*num >= '0' && *num <= '9') subscale = (subscale * 10) + (*num++ - '0'); /* Number? */
  }

  n = sign * n * pow(10.0, (scale + subscale * signsubscale));  /* number = +/- number.fraction * 10^+/- exponent */

  item->valuedouble = n;
  item->valuelong = (long long)n;
  item->type = cJSON_Number;
  return num;
}

/* Render the number nicely from the given item into a string. */
static char *print_number(cJSON *item)
{
  char *str;
  double d = item->valuedouble;
  if (fabs(((double)item->valuelong) - d) <= DBL_EPSILON && d <= INT_MAX && d >= INT_MIN) {
    str = (char*)malloc(21);  /* 2^64+1 can be represented in 21 chars. */
    if (str) sprintf(str, "%lld", item->valuelong);
  } else {
    str = (char*)malloc(64);  /* This is a nice tradeoff. */
    if (str) {
      if (fabs(floor(d) - d) <= DBL_EPSILON && fabs(d) < 1.0e60) sprintf(str, "%.0f", d);
      else if (fabs(d) < 1.0e-6 || fabs(d) > 1.0e9) sprintf(str, "%e", d);
      else sprintf(str, "%g", d);
    }
  }
  return str;
}

static unsigned parse_hex4(const char *str)
{
  unsigned h = 0;
  if (*str >= '0' && *str <= '9') h += (*str) - '0'; else if (*str >= 'A' && *str <= 'F') h += 10 + (*str) - 'A'; else if (*str >= 'a' && *str <= 'f') h += 10 + (*str) - 'a'; else return 0;
  h = h << 4; str++;
  if (*str >= '0' && *str <= '9') h += (*str) - '0'; else if (*str >= 'A' && *str <= 'F') h += 10 + (*str) - 'A'; else if (*str >= 'a' && *str <= 'f') h += 10 + (*str) - 'a'; else return 0;
  h = h << 4; str++;
  if (*str >= '0' && *str <= '9') h += (*str) - '0'; else if (*str >= 'A' && *str <= 'F') h += 10 + (*str) - 'A'; else if (*str >= 'a' && *str <= 'f') h += 10 + (*str) - 'a'; else return 0;
  h = h << 4; str++;
  if (*str >= '0' && *str <= '9') h += (*str) - '0'; else if (*str >= 'A' && *str <= 'F') h += 10 + (*str) - 'A'; else if (*str >= 'a' && *str <= 'f') h += 10 + (*str) - 'a'; else return 0;
  return h;
}

/* Parse the input text into an unescaped cstring, and populate item. */
static const char *parse_string_ptr(cJSON *item, const char *str, const char bound)
{
  static const unsigned char firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };
  const char *ptr = str + 1; char *ptr2; char *out; int len = 0; unsigned uc, uc2;
  if (*str != bound) {ep = str; return 0;}  /* not a string! */

  while (*ptr != bound && *ptr && ++len) if (*ptr++ == '\\') ptr++; /* Skip escaped quotes. */

  out = (char*)malloc(len + 1); /* This is how long we need for the string, roughly. */
  if (!out) return 0;

  ptr = str + 1; ptr2 = out;
  while (*ptr != bound && *ptr) {
    if (*ptr != '\\') *ptr2++ = *ptr++;
    else {
      ptr++;
      switch (*ptr) {
      case 'b': *ptr2++ = '\b'; break;
      case 'f': *ptr2++ = '\f'; break;
      case 'n': *ptr2++ = '\n'; break;
      case 'r': *ptr2++ = '\r'; break;
      case 't': *ptr2++ = '\t'; break;
      case 'u':  /* transcode utf16 to utf8. */
        uc = parse_hex4(ptr + 1); ptr += 4; /* get the unicode char. */

        if ((uc >= 0xDC00 && uc <= 0xDFFF) || uc == 0)  break;  /* check for invalid. */

        if (uc >= 0xD800 && uc <= 0xDBFF) { /* UTF16 surrogate pairs. */
          if (ptr[1] != '\\' || ptr[2] != 'u')  break;  /* missing second-half of surrogate.  */
          uc2 = parse_hex4(ptr + 3); ptr += 6;
          if (uc2 < 0xDC00 || uc2 > 0xDFFF)   break;  /* invalid second-half of surrogate.  */
          uc = 0x10000 + (((uc & 0x3FF) << 10) | (uc2 & 0x3FF));
        }

        len = 4; if (uc < 0x80) len = 1; else if (uc < 0x800) len = 2; else if (uc < 0x10000) len = 3; ptr2 += len;

        switch (len) {
        case 4: *--ptr2 = ((uc | 0x80) & 0xBF); uc >>= 6;
        case 3: *--ptr2 = ((uc | 0x80) & 0xBF); uc >>= 6;
        case 2: *--ptr2 = ((uc | 0x80) & 0xBF); uc >>= 6;
        case 1: *--ptr2 = (uc | firstByteMark[len]);
        }
        ptr2 += len;
        break;
      default:  *ptr2++ = *ptr; break;
      }
      ptr++;
    }
  }
  *ptr2 = 0;
  if (*ptr == bound) ptr++;
  item->valuestring = out;
  return ptr;
}

static const char *parse_string(cJSON *item, const char *str) {const char *ptr = parse_string_ptr(item, str, str[0]); item->type = cJSON_String; return ptr;}
static const char *parse_regexp(cJSON *item, const char *str) {const char *ptr = parse_string_ptr(item, str, '/'); item->type = cJSON_Regexp; return ptr;}


/* Render the cstring provided to an escaped version that can be printed. */
static char *print_string_ptr(const char *str, const char bound)
{
  const char *ptr; char *ptr2, *out; int len = 0; unsigned char token;

  if (!str) return strdup("");
  ptr = str; while ((token = *ptr) && ++len) {if (strchr("\"\\\b\f\n\r\t", token)) len++; else if (token < 32) len += 5; ptr++;}

  out = (char*)malloc(len + 3);
  if (!out) return 0;

  ptr2 = out; ptr = str;
  *ptr2++ = bound;
  while (*ptr) {
    if ((unsigned char)*ptr > 31 && *ptr != bound && *ptr != '\\') *ptr2++ = *ptr++;
    else {
      *ptr2++ = '\\';
      switch (token = *ptr++) {
      case '\\':  *ptr2++ = '\\'; break;
      case '\"':  *ptr2++ = '\"'; break;
      case '\b':  *ptr2++ = 'b';  break;
      case '\f':  *ptr2++ = 'f';  break;
      case '\n':  *ptr2++ = 'n';  break;
      case '\r':  *ptr2++ = 'r';  break;
      case '\t':  *ptr2++ = 't';  break;
      default: sprintf(ptr2, "u%04x", token); ptr2 += 5;  break;  /* escape and print */
      }
    }
  }
  *ptr2++ = bound; *ptr2++ = 0;
  return out;
}
/* Invote print_string_ptr (which is useful) on an item. */
static char *print_string(cJSON *item) {return print_string_ptr(item->valuestring, '\"');}
static char *print_regexp(cJSON *item) {return print_string_ptr(item->valuestring, '/');}

/* Predeclare these prototypes. */
static const char *parse_value(cJSON *item, const char *value);
static char *print_value(cJSON *item, int depth, int fmt);
static const char *parse_array(cJSON *item, const char *value);
static char *print_array(cJSON *item, int depth, int fmt);
static const char *parse_object(cJSON *item, const char *value);
static char *print_object(cJSON *item, int depth, int fmt);

/* Utility to jump whitespace and cr/lf */
static const char *skip(const char *in)
{
  while (in && *in && (unsigned char)*in <= 32)
    in++;
  return in;
}

/* Parse an object - create a new root, and populate. */
static cJSON *cJSON_Parse(const char *value)
{
  const char * end = 0;
  cJSON * c = cJSON_New();
  ep = 0;
  if (!c) return 0;       /* memory fail */

  end = parse_value(c, skip(value));
  if (!end) {cJSON_Delete(c); return 0;}  /* parse failure. ep is set. */

  return c;
}

/* Render a cJSON item/entity/structure to text. */
static char *cJSON_Print(cJSON *item) {return print_value(item, 0, 1);}

/* Parser core - when encountering text, process appropriately. */
static const char *parse_value(cJSON *item, const char *value)
{
  if (!value) return 0; /* Fail on null. */
  if (!strncmp(value, "null", 4)) { item->type = cJSON_Null; return value + 4; }
  if (!strncmp(value, "false", 5)) { item->type = cJSON_False; return value + 5; }
  if (!strncmp(value, "true", 4)) { item->type = cJSON_True; item->valuelong = 1; return value + 4; }
  if (*value == '-' || (*value >= '0' && *value <= '9')) { return parse_number(item, value); }
  if (*value == '\"' || *value == '\'') { return parse_string(item, value); }
  if (*value == '/') { return parse_regexp(item, value); }
  if (*value == '[') { return parse_array(item, value); }
  if (*value == '{') { return parse_object(item, value); }

  ep = value; return 0; /* failure. */
}

/* Render a value to text. */
static char *print_value(cJSON *item, int depth, int fmt)
{
  char *out = 0;
  if (!item) return 0;
  switch ((item->type) & 255) {
  case cJSON_Null:   out = strdup("null"); break;
  case cJSON_False:  out = strdup("false"); break;
  case cJSON_True:   out = strdup("true"); break;
  case cJSON_Number: out = print_number(item); break;
  case cJSON_String: out = print_string(item); break;
  case cJSON_Regexp: out = print_regexp(item); break;
  case cJSON_Array:  out = print_array(item, depth, fmt); break;
  case cJSON_Object: out = print_object(item, depth, fmt); break;
  }
  if (item->diff) {
    char *t = (char *)malloc(strlen(out) + 16);
    if (t) {
      sprintf(t, "%s %s", item->diff == 1 ? ">>>>>>>" : "<<<<<<<", out);
      free(out);
      out = t;
    }
  }
  return out;
}

/* Build an array from input text. */
static const char *parse_array(cJSON *item, const char *value)
{
  cJSON *child;
  if (*value != '[')  {ep = value; return 0;} /* not an array! */

  item->type = cJSON_Array;
  value = skip(value + 1);
  if (*value == ']') return value + 1;  /* empty array. */

  item->child = child = cJSON_New();
  if (!item->child) return 0;    /* memory fail */
  value = skip(parse_value(child, skip(value)));  /* skip any spacing, get the value. */
  if (!value) return 0;

  while (*value == ',') {
    cJSON *new_item;
    if (!(new_item = cJSON_New())) return 0;  /* memory fail */
    child->next = new_item; new_item->prev = child; child = new_item;
    value = skip(parse_value(child, skip(value + 1)));
    if (!value) return 0; /* memory fail */
  }

  if (*value == ']') return value + 1;  /* end of array */
  ep = value; return 0; /* malformed. */
}

/* Render an array to text */
static char *print_array(cJSON *item, int depth, int fmt)
{
  char **entries;
  char *out = 0, *ptr, *ret; int len = 5;
  cJSON *child = item->child;
  int numentries = 0, i = 0, fail = 0;

  /* How many entries in the array? */
  while (child) numentries++, child = child->next;
  /* Explicitly handle numentries==0 */
  if (!numentries) {
    out = (char*)malloc(3);
    if (out) strcpy(out, "[]");
    return out;
  }
  /* Allocate an array to hold the values for each */
  entries = (char**)malloc(numentries * sizeof(char*));
  if (!entries) return 0;
  memset(entries, 0, numentries * sizeof(char*));
  /* Retrieve all the results: */
  child = item->child;
  while (child && !fail) {
    ret = print_value(child, depth + 1, fmt);
    entries[i++] = ret;
    if (ret) len += strlen(ret) + 2 + (fmt ? 1 : 0); else fail = 1;
    child = child->next;
  }

  /* If we didn't fail, try to malloc the output string */
  if (!fail) out = (char*)malloc(len);
  /* If that fails, we fail. */
  if (!out) fail = 1;

  /* Handle failure. */
  if (fail) {
    for (i = 0; i < numentries; i++) if (entries[i]) free(entries[i]);
    free(entries);
    return 0;
  }

  /* Compose the output array. */
  *out = '[';
  ptr = out + 1; *ptr = 0;
  for (i = 0; i < numentries; i++) {
    strcpy(ptr, entries[i]); ptr += strlen(entries[i]);
    if (i != numentries - 1) {*ptr++ = ','; if (fmt)*ptr++ = ' '; *ptr = 0;}
    free(entries[i]);
  }
  free(entries);
  *ptr++ = ']'; *ptr++ = 0;
  return out;
}

/* Build an object from the text. */
static const char *parse_object(cJSON *item, const char *value)
{
  cJSON *child;
  if (*value != '{')  {ep = value; return 0;} /* not an object! */

  item->type = cJSON_Object;
  value = skip(value + 1);
  if (*value == '}') return value + 1;  /* empty array. */

  item->child = child = cJSON_New();
  if (!item->child) return 0;
  value = skip(parse_string(child, skip(value)));
  if (!value) return 0;
  child->keystring = child->valuestring; child->valuestring = 0;
  if (*value != ':') {ep = value; return 0;}  /* fail! */
  value = skip(parse_value(child, skip(value + 1)));  /* skip any spacing, get the value. */
  if (!value) return 0;

  while (*value == ',') {
    cJSON *new_item;
    if (!(new_item = cJSON_New()))  return 0; /* memory fail */
    child->next = new_item; new_item->prev = child; child = new_item;
    value = skip(parse_string(child, skip(value + 1)));
    if (!value) return 0;
    child->keystring = child->valuestring; child->valuestring = 0;
    if (*value != ':') {ep = value; return 0;}  /* fail! */
    value = skip(parse_value(child, skip(value + 1)));  /* skip any spacing, get the value. */
    if (!value) return 0;
  }

  if (*value == '}') return value + 1;  /* end of array */
  ep = value; return 0; /* malformed. */
}

/* Render an object to text. */
static char *print_object(cJSON *item, int depth, int fmt)
{
  char **entries = 0, **names = 0;
  char *out = 0, *ptr, *ret, *str; int len = 7, i = 0, j;
  cJSON *child = item->child;
  int numentries = 0, fail = 0;
  /* Count the number of entries. */
  while (child) numentries++, child = child->next;
  /* Explicitly handle empty object case */
  if (!numentries) {
    out = (char*)malloc(fmt ? depth + 4 : 3);
    if (!out) return 0;
    ptr = out; *ptr++ = '{';
    if (fmt) {*ptr++ = '\n'; for (i = 0; i < depth - 1; i++) *ptr++ = '\t';}
    *ptr++ = '}'; *ptr++ = 0;
    return out;
  }
  /* Allocate space for the names and the objects */
  entries = (char**)malloc(numentries * sizeof(char*));
  if (!entries) return 0;
  names = (char**)malloc(numentries * sizeof(char*));
  if (!names) {free(entries); return 0;}
  memset(entries, 0, sizeof(char*)*numentries);
  memset(names, 0, sizeof(char*)*numentries);

  /* Collect all the results into our arrays: */
  child = item->child; depth++; if (fmt) len += depth;
  while (child) {
    names[i] = str = print_string_ptr(child->keystring, '\"');
    entries[i++] = ret = print_value(child, depth, fmt);
    if (str && ret) len += strlen(ret) + strlen(str) + 2 + (fmt ? 2 + depth : 0); else fail = 1;
    child = child->next;
  }

  /* Try to allocate the output string */
  if (!fail) out = (char*)malloc(len);
  if (!out) fail = 1;

  /* Handle failure */
  if (fail) {
    for (i = 0; i < numentries; i++) {if (names[i]) free(names[i]); if (entries[i]) free(entries[i]);}
    free(names); free(entries);
    return 0;
  }

  /* Compose the output: */
  *out = '{'; ptr = out + 1; if (fmt)*ptr++ = '\n'; *ptr = 0;
  for (i = 0; i < numentries; i++) {
    if (fmt) for (j = 0; j < depth; j++) *ptr++ = '\t';
    strcpy(ptr, names[i]); ptr += strlen(names[i]);
    *ptr++ = ':'; if (fmt) *ptr++ = '\t';
    strcpy(ptr, entries[i]); ptr += strlen(entries[i]);
    if (i != numentries - 1) *ptr++ = ',';
    if (fmt) *ptr++ = '\n'; *ptr = 0;
    free(names[i]); free(entries[i]);
  }

  free(names); free(entries);
  if (fmt) for (i = 0; i < depth - 1; i++) *ptr++ = '\t';
  *ptr++ = '}'; *ptr++ = 0;
  return out;
}


/* Get Array size/item */
static int    cJSON_GetArraySize(cJSON *array) {cJSON *c = array->child; int i = 0; while (c)i++, c = c->next; return i;}
static cJSON *cJSON_GetArrayItem(cJSON *array, int index) {cJSON *c = array->child; while (c && index > 0) index--, c = c->next; return c;}

/* Get Object size/item */
static int    cJSON_GetObjectSize(cJSON *object) {cJSON *c = object->child; int i = 0; while (c)i++, c = c->next; return i;}
static cJSON *cJSON_GetObjectItem(cJSON *object, int index) {cJSON *c = object->child; while (c && index > 0) index--, c = c->next; return c;}
static cJSON *cJSON_GetObjectItem(cJSON *object, const char *key) {cJSON *c = object->child; while (c && (!c->keystring || strcasecmp(c->keystring, key))) c = c->next; return c;}



/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////cJSON_compare.c/////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////



static int __cJSON_Compare(cJSON *, cJSON *, int *);
static int __cJSON_CompareArray(cJSON *, cJSON *, int *);
static int __cJSON_CompareObject(cJSON *, cJSON *, int *);


static int __cJSON_CompareArray(cJSON *cexp, cJSON *cact, int *diffed)
{
  if (!cexp || !cact) {
    if (!*diffed) {*diffed = 1; cexp && (cexp->diff = 1); cact && (cact->diff = 2);}
    return -1;
  }
  int exp_size = cJSON_GetArraySize(cexp);
  int act_size = cJSON_GetArraySize(cact);
  if (exp_size != act_size) {
    if (!*diffed) {*diffed = 1; cexp->diff = 1; cact->diff = 2;}
    return 1;
  }
  for (int i = 0; i < exp_size; i++) {
    cJSON *c1  = cJSON_GetArrayItem(cexp, i);
    if (c1 == NULL) {
      return -1;
    }
    cJSON *c2 = cJSON_GetArrayItem(cact, i);
    if (c2 == NULL) {
      return -1;
    }
    int cmp = __cJSON_Compare(c1, c2, diffed);
    if (cmp != 0) {
      return cmp;
    }
  }
  return 0;
}

static int __cJSON_CompareObject(cJSON *cexp, cJSON *cact, int *diffed)
{
  if (!cexp || !cact) {
    if (!*diffed) {*diffed = 1; cexp && (cexp->diff = 1); cact && (cact->diff = 2);}
    return -1;
  }
  int exp_size = cJSON_GetObjectSize(cexp);
  int act_size = cJSON_GetObjectSize(cact);
  if (exp_size > act_size) {
    if (!*diffed) {*diffed = 1; cexp->diff = 1; cact->diff = 2;}
    return 1;
  }

  for (int i = 0; i < exp_size; i++) {
    cJSON *c1 = cJSON_GetObjectItem(cexp, i);
    if (c1 == NULL) {
      return -1;
    }
    cJSON *c2 = cJSON_GetObjectItem(cact, c1->keystring);
    if (c2 == NULL) {
      return -1;
    }

    int cmp = __cJSON_Compare(c1, c2, diffed);
    if (cmp != 0) {
      return cmp;
    }
  }
  return 0;
}


static int __cJSON_Compare(cJSON *cexp, cJSON *cact, int *diffed)
{
  if (!cexp || !cact) {
    if (!*diffed) {*diffed = 1; cexp && (cexp->diff = 1); cact && (cact->diff = 2);}
    return -1;
  }

  int cmp = 0;
  switch (cexp->type) {
  case cJSON_False:
    cmp = (cact->type == cJSON_False) ? 0 : 1;
    break;
  case cJSON_True:
    cmp = (cact->type == cJSON_True) ? 0 : 1;
    break;
  case cJSON_Null:
    cmp = (cact->type == cJSON_Null) ? 0 : 1;
    break;

  case cJSON_Number:
    if (cact->type != cJSON_Number) cmp = 1;
    else cmp = cexp->valuelong == cact->valuelong && fabs(cexp->valuedouble - cact->valuedouble) < 0.00001 ? 0 : 1;
    break;

  case cJSON_String:
    if (cact->type != cJSON_String) cmp = 1;
    else cmp = strcmp(cexp->valuestring, cact->valuestring);
    break;

  case cJSON_Regexp:
    if (cact->type != cJSON_String) cmp = 1;
    else cmp = __wildcard_match(cexp->valuestring, cact->valuestring) || SLRE::slre_match(cexp->valuestring, cact->valuestring, strlen(cact->valuestring), NULL, 0, 0) >= 0 ? 0 : 1;
    break;

  case cJSON_Array:
    if (cact->type != cJSON_Array) cmp = 1;
    else cmp = __cJSON_CompareArray(cexp, cact, diffed);
    break;

  case cJSON_Object:
    if (cact->type != cJSON_Object) cmp = 1;
    else cmp = __cJSON_CompareObject(cexp, cact, diffed);
    break;

  default:
    break;
  }

  if (cmp != 0) {
    if (!*diffed) {*diffed = 1; cexp->diff = 1; cact->diff = 2;}
  }
  return cmp;
}

static int cJSON_Compare(char *sexp, char *sact, char **fexp, char **fact)
{
  int cmp = -1;
  cJSON *cexp = cJSON_Parse(sexp);
  cJSON *cact = cJSON_Parse(sact);
  if (cexp && cact) {
    int diffed = 0;
    cmp = __cJSON_Compare(cexp, cact, &diffed);
  }

  if (cexp) {
    char *t = cJSON_Print(cexp);
    *fexp = t;
    cJSON_Delete(cexp);
  }
  if (cact) {
    char *t = cJSON_Print(cact);
    *fact = t;
    cJSON_Delete(cact);
  }


  return cmp;
}

}


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
///////////////////////////////// H2UNIT ////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////


class h2unit_config
{
public:
  bool _colored;
  bool _verbose;
  bool _random;
  char* _include;
  char* _exclude;
  char* _path;

  h2unit_config()
  {
    _verbose = false;
    _colored = true;
    _random = false;
    _include = NULL;
    _exclude = NULL;
  }
} __cfg;


static jmp_buf __h2unit_jmp_buf;

typedef struct h2unit_blob {
  h2unit_list queue;
  h2unit_list stack;

  void* ptr;
  unsigned char* data;
  size_t size;
  const char* file;
  int line;
} h2unit_blob;

static const int BLOB_GUARD_SIZE = 8;
static const unsigned BLOB_MAGIC_CODE = 0xbeafcafe;

h2unit_blob* h2unit_blob_new(size_t size, size_t alignment, unsigned char c, const char* file, int line)
{
  void* data = malloc(size + BLOB_GUARD_SIZE * sizeof(unsigned) * 2 + alignment);
  if (!data) return NULL;
  h2unit_blob* new_blob = (h2unit_blob*) malloc(sizeof(h2unit_blob));
  if (!new_blob) {
    free(data);
    return NULL;
  }
  new_blob->data = (unsigned char*)data;
  new_blob->ptr = new_blob->data + BLOB_GUARD_SIZE * sizeof(unsigned);
  new_blob->ptr = (void*)(((unsigned long long)(new_blob->ptr) + ((alignment) - 1)) & (~((alignment) - 1)));
  new_blob->size = size;
  new_blob->file = file;
  new_blob->line = line;

  h2unit_list_init(&new_blob->queue);
  h2unit_list_init(&new_blob->stack);

  for (int l = 0; l < (int)size; l++) {
    ((unsigned char*)new_blob->ptr)[l] = c;
  }

  unsigned* p = (unsigned*)(new_blob->ptr) - 1;
  unsigned* q = (unsigned*)((unsigned char*)(new_blob->ptr) + new_blob->size);
  for (int i = 0; i < BLOB_GUARD_SIZE; i++) {
    p[-i] = BLOB_MAGIC_CODE;
    q[i] = BLOB_MAGIC_CODE;
  }

  return new_blob;
}

void h2unit_blob_del(h2unit_blob* blob)
{
  h2unit_list_init(&blob->queue);
  h2unit_list_init(&blob->stack);

  /* overflow and under-flow checking */
  bool overed = false;
  unsigned* p = (unsigned*)(blob->ptr) - 1;
  unsigned* q = (unsigned*)((unsigned char*)(blob->ptr) + blob->size);
  for (int i = 0; i < BLOB_GUARD_SIZE; i++) {
    if (p[-i] != BLOB_MAGIC_CODE || q[i] != BLOB_MAGIC_CODE) {
      if (h2unit_case::_current_) {
        h2unit_case::_current_->_vmsg_(&h2unit_case::_current_->_errormsg_, "bold,red", q[i] == BLOB_MAGIC_CODE ? "Memory OverFlow" : "Memory UnderFlow");
        h2unit_case::_current_->_vmsg_(&h2unit_case::_current_->_errormsg_, "", " at %s:%d", blob->file, blob->line);
        overed = true;
        break;
      }
    }
  }

  free(blob->data);
  free(blob);

  if (overed) {
    longjmp(__h2unit_jmp_buf, 1);
  }
}

typedef struct h2unit_leak {
  h2unit_list stack;
  h2unit_list blobs;

  const char* file;
  int line;
} h2unit_leak;

typedef struct h2unit_symb {
  char* raw;
  char* join;
  char* name;
  int argc;
  char* argv[32];

  void* addr;
  h2unit_list link;
} h2unit_symb;

static void __trim_total_whitespace(char* s)
{
  int len = strlen(s);
  for (char* p = s; *p; p++, len--) {
    while (isspace(*p)) memmove(p, p + 1, len--);
  }
}

static void __trim_sides_whitespace(char* s)
{
  char* p;
  for (p = s; isspace(*p); p++);

  for (char* q = p + strlen(p) - 1; isspace(*q); q--) {
    *q = '\0';
  }

  if (s != p) {
    memmove(s, p, strlen(p) + 1);
  }
}

void h2unit_symb_parse(h2unit_symb* symb, char* raw)
{
#if defined(__APPLE__) || defined(macintosh)
  symb->raw = strdup(raw + 1);  /* prefix _ to all symbols in MAC OS */
#else
  symb->raw = strdup(raw);
#endif
  __trim_sides_whitespace(symb->raw);
  symb->join = strdup(symb->raw);
  __trim_total_whitespace(symb->join);
  char* t = strdup(symb->raw);
  symb->name = strdup(strtok(t, "("));
  __trim_sides_whitespace(symb->name);
  symb->argc = -1;
  char* r = strtok(NULL, ")");
  if (r) {
    symb->argc = 0;
    char* p = strtok(r, ",");
    for (unsigned i = 0; p && i < sizeof(symb->argv) / sizeof(symb->argv[0]); p = strtok(NULL, ",")) {
      __trim_total_whitespace((symb->argv[symb->argc++] = strdup(p)));
    }
  }
  free(t);
}

void h2unit_symb_release(h2unit_symb* symb)
{
  if (symb->raw) free(symb->raw);
  if (symb->join) free(symb->join);
  if (symb->name) free(symb->name);

  for (int i = 0; i < symb->argc; i++) {
    if (symb->argv[i]) free(symb->argv[i]);
  }
}

double h2unit_symb_compare(h2unit_symb* a, h2unit_symb* b)
{
  double similarity = 0.0;
  if (!strcmp(a->raw, b->raw)) {
    return 1.0;
  }
  if (!strcmp(a->join, b->join)) {
    return 1.0;
  }
  if (strcmp(a->name, b->name)) {
    return 0.0; /* function name is unmatch, absolutely not same */
  } else {
    similarity += 0.6;
  }
  if (a->argc != b->argc) {
    return similarity;
  }
  similarity += 0.2;
  double ds = 0.2 / a->argc;
  for (int i = 0; i < a->argc; i++) {
    if (!strcmp(a->argv[i], b->argv[i])) {
      similarity += ds;
    }
  }

  return similarity;
}

typedef struct h2unit_stub {
  h2unit_list link;
  void* native;
#if defined(__x86_64__) || defined(_M_X64)
  unsigned char saved_code[sizeof(void*) + 4];
#elif defined(__i386__) || defined(_M_IX86)
  unsigned char saved_code[sizeof(void*) + 1];
#else
  unsigned char saved_code[1];
#endif
} h2unit_stub;

h2unit_stub* h2unit_stub_new()
{
  h2unit_stub* stub = (h2unit_stub*) malloc(sizeof(h2unit_stub));
  if (stub) {
    memset(stub, 0, sizeof(h2unit_stub));
    h2unit_list_init(&stub->link);
  }
  return stub;
}

char* h2unit_stub_save(h2unit_stub* stub, void* native)
{
  static char reason[128];
#ifdef _WIN32
  DWORD saved;
  if (!VirtualProtect(native, sizeof(void*) + 4, PAGE_EXECUTE_READWRITE, &saved)) { // PAGE_EXECUTE_WRITECOPY OR PAGE_WRITECOPY
    sprintf(reason, "VirtualProtect:%d", GetLastError());
    return reason;
  }
#else
  int pagesize = sysconf(_SC_PAGE_SIZE);
  if (mprotect((void*) ((unsigned long) native & (~(pagesize - 1))), pagesize, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
    sprintf(reason, "mprotect:%s", strerror(errno));
    return reason;
  }
#endif

  stub->native = native;
  memcpy(stub->saved_code, native, sizeof(stub->saved_code));
  return NULL;
}

void h2unit_stub_set(h2unit_stub* stub, void* fake)
{
  unsigned char *I = (unsigned char*) stub->native;

  //x86 __asm("jmp $fake") : 0xE9 {offset=fake-native-5}
  //x86 __asm("movl $fake, %eax; jmpl %eax") : 0xB8 {fake} 0xFF 0xE0
  //x86_64 __asm("movq $fake, %rax; jmpq %rax") : 0x48 0xB8 {fake} 0xFF 0xE0
#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64)
  long delta = (long) fake - (long) stub->native;
# if defined(__x86_64__) || defined(_M_X64)
  if (delta < (int) (-2147483647 - 1) || (int) 2147483647 < delta) {
    *I++ = 0x48;
    *I++ = 0xB8;
    memcpy(I, &fake, sizeof(void*));
    I += sizeof(void*);
    *I++ = 0xFF;
    *I++ = 0xE0;
    return;
  }
# endif

  int offset = delta - 5;
  *I++ = 0xE9;
  memcpy(I, (void*)&offset, sizeof(offset));

#elif defined(__powerpc__)
#else
#endif
}

void h2unit_stub_del(h2unit_stub* stub)
{
  memcpy(stub->native, stub->saved_code, sizeof(stub->saved_code)); /* restore saved */
  free(stub);
}

typedef struct h2unit_string {
  const char* style;
  char* data;
  h2unit_list link;
} h2unit_string;

typedef struct h2unit_unit {
  const char* name;
  int case_count;
  int case_passed;
  int case_failed;
  int case_todo;
  int case_filtered;
  h2unit_case* cases;
  struct h2unit_unit* next;
} h2unit_unit;

class h2unit_listen
{
public:
  class h2unit_listen* next;
public:
  h2unit_listen()
  {
    next = NULL;
  }
  virtual ~h2unit_listen()
  {
  }
  virtual void on_task_start()
  {
  }
  virtual void on_task_endup(int failed, int passed, int todo, int filtered, int cases, int checks, long duration, h2unit_unit* unit_list)
  {
    for (h2unit_unit* p = unit_list; p; p = p->next) {
      for (h2unit_case* c = p->cases; c; c = c->_next_) {
        p->case_count++;
        switch (c->_status_) {
        case h2unit_case::_TODOED_:
          p->case_todo++;
          break;
        case h2unit_case::_FILTED_:
          p->case_filtered++;
          break;
        case h2unit_case::_PASSED_:
          p->case_passed++;
          break;
        case h2unit_case::_FAILED_:
          p->case_failed++;
          break;
        }
      }
    }
  }
  virtual void on_case_start()
  {
  }
  virtual void on_case_endup()
  {
  }
};

class h2unit_listens: public h2unit_listen
{
public:
  h2unit_listens()
  {
  }
  void attach(h2unit_listen* listener)
  {
    listener->next = next;
    next = listener;
  }
  void on_task_start()
  {
    for (class h2unit_listen *p = next; p; p = p->next) {
      p->on_task_start();
    }
  }
  void on_task_endup(int failed, int passed, int todo, int filtered, int cases, int checks, long duration, h2unit_unit* unit_list)
  {
    for (class h2unit_listen *p = next; p; p = p->next) {
      p->on_task_endup(failed, passed, todo, filtered, cases, checks, duration, unit_list);
    }
  }
  void on_case_start()
  {
    for (class h2unit_listen *p = next; p; p = p->next) {
      p->on_case_start();
    }
  }
  void on_case_endup()
  {
    for (class h2unit_listen *p = next; p; p = p->next) {
      p->on_case_endup();
    }
  }
};

class h2unit_listen_text: public h2unit_listen
{
private:
  FILE *filp;
  void print_string(h2unit_list* s)
  {
    h2unit_list* p;
    h2unit_list_for_each(p, s) {
      h2unit_string* r = h2unit_list_entry(p, h2unit_string, link);
      fprintf(filp, "%s", r->data);
    }
  }
public:
  h2unit_listen_text()
  {
  }
  virtual ~h2unit_listen_text()
  {
  }
  void on_task_start()
  {
    filp = fopen("h2unit_text.log", "w");
  }
  void on_task_endup(int failed, int passed, int todo, int filtered, int cases, int checks, long duration, h2unit_unit* unit_list)
  {
    if (failed > 0) {
      fprintf(filp, "\nFailed <%d failed, %d passed, %d todo, %d filtered, %d checks, %ld ms>\n", failed, passed, todo, filtered, checks, duration);
    } else {
      fprintf(filp, "\nPassed <%d passed, %d todo, %d filtered, %d cases, %d checks, %ld ms>\n", passed, todo, filtered, cases, checks, duration);
    }
    fclose(filp);
  }
  void on_case_endup()
  {
    h2unit_case* p = h2unit_case::_current_;
    switch (p->_status_) {
    case h2unit_case::_TODOED_:
      if (!strlen(p->_unitname_)) {
        fprintf(filp, "H2UNIT_CASE(%s): TODO at %s:%d\n", p->_casename_, p->_casefile_, p->_caseline_);
      } else {
        fprintf(filp, "H2CASE(%s, %s): TODO at %s:%d\n", p->_unitname_, p->_casename_, p->_casefile_, p->_caseline_);
      }
      break;
    case h2unit_case::_FILTED_:
      break;
    case h2unit_case::_PASSED_:
      if (__cfg._verbose) {
        if (!strlen(p->_unitname_)) {
          fprintf(filp, "H2UNIT_CASE(%s): Passed - %ld ms     \n", p->_casename_, p->_endup_ - p->_start_);
        } else {
          fprintf(filp, "H2CASE(%s, %s): Passed - %ld ms     \n", p->_unitname_, p->_casename_, p->_endup_ - p->_start_);
        }
      }
      break;
    case h2unit_case::_FAILED_:
      if (!strlen(p->_unitname_)) {
        fprintf(filp, "H2UNIT_CASE(%s): Failed at %s:%d\n", p->_casename_, p->_checkfile_, p->_checkline_);
      } else {
        fprintf(filp, "H2CASE(%s, %s): Failed at %s:%d\n", p->_unitname_, p->_casename_, p->_checkfile_, p->_checkline_);
      }
      if (!h2unit_list_empty(&p->_errormsg_)) {
        fprintf(filp, "  ");
        print_string(&h2unit_case::_current_->_errormsg_);
        fprintf(filp, "\n");
      }
      if (!h2unit_list_empty(&p->_expected_)) {
        fprintf(filp, "  expected<");
        print_string(&h2unit_case::_current_->_expected_);
        fprintf(filp, ">\n");
      }
      if (!h2unit_list_empty(&p->_unexpect_)) {
        fprintf(filp, "  unexpect<");
        print_string(&h2unit_case::_current_->_unexpect_);
        fprintf(filp, ">\n");
      }
      if (!h2unit_list_empty(&p->_actually_)) {
        fprintf(filp, "  actually<");
        print_string(&h2unit_case::_current_->_actually_);
        fprintf(filp, ">\n");
      }
      if (p->_addition_) {
        for (int i = 0; !h2unit_list_empty(&p->_addition_[i]); i++) {
          fprintf(filp, "   ");
          print_string(&p->_addition_[i]);
          fprintf(filp, "\n");
        }
      }
      break;
    }
  }

};

class h2unit_listen_console: public h2unit_listen
{
private:

  struct st {
    const char *name;
    const int value;
  };

#if defined(_WIN32)

  void color(const char *style)
  {
    static HANDLE console_handle = NULL;
    static WORD default_attribute;

    if (console_handle == NULL) {
      console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
      CONSOLE_SCREEN_BUFFER_INFO csbi;
      GetConsoleScreenBufferInfo(console_handle, &csbi);
      default_attribute = csbi.wAttributes;
    }
    if (!__cfg._colored) return;

    static struct st t[] = {
      // normal style
      { "reset", default_attribute },
      { "bold", FOREGROUND_INTENSITY },
      // { "italics", 3 },
      { "underline", COMMON_LVB_UNDERSCORE },
      { "inverse", COMMON_LVB_REVERSE_VIDEO },
      // { "strikethrough", 9 },
      // foreground color
      // { "black", 30 },
      { "red", FOREGROUND_RED },
      { "green", FOREGROUND_GREEN },
      { "yellow", FOREGROUND_RED | FOREGROUND_GREEN },
      { "blue", FOREGROUND_BLUE },
      { "purple", FOREGROUND_RED | FOREGROUND_BLUE },
      { "cyan", FOREGROUND_BLUE | FOREGROUND_GREEN },
      { "white", FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE },
      { "default", 39 },
      // background color
      // { "bg_black", 40 },
      { "bg_red", BACKGROUND_RED },
      { "bg_green", BACKGROUND_GREEN },
      { "bg_yellow", BACKGROUND_RED | BACKGROUND_GREEN },
      { "bg_blue", BACKGROUND_BLUE },
      { "bg_purple", BACKGROUND_RED | BACKGROUND_BLUE },
      { "bg_cyan", BACKGROUND_BLUE | BACKGROUND_GREEN},
      { "bg_white", BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE },
      // { "bg_default", 49 }
    };

    static char copied[512];
    strncpy(copied, style, sizeof(copied));

    WORD a = 0;

    for (char* opt = strtok(copied, ","); opt; opt = strtok(NULL, ",")) {
      for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        if (strcmp(t[i].name, opt) == 0) {
          a |= t[i].value;
          break;
        }
      }
    }

    SetConsoleTextAttribute(console_handle, a);
  }

#else

  void color(const char *style)
  {
    if (!__cfg._colored) return;

    static struct st t[] = {
      // normal style
      { "reset", 0 },
      { "bold", 1 },
      { "italics", 3 },
      { "underline", 4 },
      { "inverse", 7 },
      { "strikethrough", 9 },
      // foreground color
      { "black", 30 },
      { "red", 31 },
      { "green", 32 },
      { "yellow", 33 },
      { "blue", 34 },
      { "purple", 35 },
      { "cyan", 36 },
      { "white", 37 },
      { "default", 39 },
      // background color
      { "bg_black", 40 },
      { "bg_red", 41 },
      { "bg_green", 42 },
      { "bg_yellow", 43 },
      { "bg_blue", 44 },
      { "bg_purple", 45 },
      { "bg_cyan", 46 },
      { "bg_white", 47 },
      { "bg_default", 49 }
    };

    static char copied[512];
    strncpy(copied, style, sizeof(copied));

    static char buffer[128];
    char *p = buffer;
    p += sprintf(p, "\033[");

    for (char* opt = strtok(copied, ","); opt; opt = strtok(NULL, ",")) {
      for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        if (strcmp(t[i].name, opt) == 0) {
          p += sprintf(p, "%d;", t[i].value);
          break;
        }
      }
    }

    *(p - 1) = 'm';
    printf("%s", buffer);
  }

#endif

  void print_string(h2unit_list* s)
  {
    h2unit_list* p;
    h2unit_list_for_each(p, s) {
      h2unit_string* r = h2unit_list_entry(p, h2unit_string, link);
      color(r->style);
      printf("%s", r->data);
      color("reset");
    }
  }

public:
  h2unit_listen_console()
  {
  }
  virtual ~h2unit_listen_console()
  {
  }
  void on_task_start()
  {
  }
  void on_task_endup(int failed, int passed, int todo, int filtered, int cases, int checks, long duration, h2unit_unit* unit_list)
  {
    if (failed > 0) {
      color("bold,red");
      printf("\nFailed <%d failed, %d passed, %d todo, %d filtered, %d checks, %ld ms>\n", failed, passed, todo, filtered, checks, duration);
    } else {
      color("bold,green");
      printf("\nPassed <%d passed, %d todo, %d filtered, %d cases, %d checks, %ld ms>\n", passed, todo, filtered, cases, checks, duration);
    }
    color("reset");
  }
  void on_case_endup()
  {
    h2unit_case* p = h2unit_case::_current_;
    switch (p->_status_) {
    case h2unit_case::_TODOED_:
      if (!strlen(p->_unitname_)) {
        printf("H2UNIT_CASE(%s): TODO at %s:%d\n", p->_casename_, p->_casefile_, p->_caseline_);
      } else {
        printf("H2CASE(%s, %s): TODO at %s:%d\n", p->_unitname_, p->_casename_, p->_casefile_, p->_caseline_);
      }
      break;
    case h2unit_case::_FILTED_:
      break;
    case h2unit_case::_PASSED_:
      if (__cfg._verbose) {
        color("blue");
        if (!strlen(p->_unitname_)) {
          printf("H2UNIT_CASE(%s): Passed - %ld ms\n", p->_casename_, p->_endup_ - p->_start_);
        } else {
          printf("H2CASE(%s, %s): Passed - %ld ms\n", p->_unitname_, p->_casename_, p->_endup_ - p->_start_);
        }
        color("reset");
      }
      break;
    case h2unit_case::_FAILED_:
      color("bold,purple");
      if (!strlen(p->_unitname_)) {
        printf("H2UNIT_CASE(%s): Failed at %s:%d\n", p->_casename_, p->_checkfile_, p->_checkline_);
      } else {
        printf("H2CASE(%s, %s): Failed at %s:%d\n", p->_unitname_, p->_casename_, p->_checkfile_, p->_checkline_);
      }
      color("reset");
      if (!h2unit_list_empty(&p->_errormsg_)) {
        printf("  ");
        print_string(&p->_errormsg_);
        printf("\n");
      }
      if (!h2unit_list_empty(&p->_expected_)) {
        printf("  expected<");
        print_string(&p->_expected_);
        printf(">\n");
      }
      if (!h2unit_list_empty(&p->_unexpect_)) {
        printf("  unexpect<");
        print_string(&p->_unexpect_);
        printf(">\n");
      }
      if (!h2unit_list_empty(&p->_actually_)) {
        printf("  actually<");
        print_string(&p->_actually_);
        printf(">\n");
      }
      if (p->_addition_) {
        for (int i = 0; !h2unit_list_empty(&p->_addition_[i]); i++) {
          printf("   ");
          print_string(&p->_addition_[i]);
          printf("\n");
        }
      }
      break;
    }

    color("reset");
  }
};

class h2unit_listen_html: public h2unit_listen
{
private:
  FILE *filp;
public:
  h2unit_listen_html()
  {
  }
  virtual ~h2unit_listen_html()
  {
  }
  void on_task_start()
  {
    filp = fopen("h2unit_html.html", "w");
    fprintf(filp, "<html>");
    fprintf(filp, "<head>");
    fprintf(filp, "<title></title>");
    fprintf(filp, "<style></style>");
    fprintf(filp, "</head>");
    fprintf(filp, "<body>");
    fprintf(filp, "<table>");

  }
  void on_task_endup(int failed, int passed, int todo, int filtered, int cases, int checks, long duration, h2unit_unit* unit_list)
  {
    for (h2unit_unit* p = unit_list; p; p = p->next) {
      fprintf(filp, "<tr>");
      fprintf(filp, "<td> %s </td>", p->name);
      fprintf(filp, "<td><table>");
      for (h2unit_case* c = p->cases; c; c = c->_next_) {
        fprintf(filp, "<tr>");
        const char* status;
        switch (c->_status_) {
        case h2unit_case::_TODOED_:
          status = "TODO";
          break;
        case h2unit_case::_FILTED_:
          status = "Filtered";
          break;
        case h2unit_case::_PASSED_:
          status = "Passed";
          break;
        case h2unit_case::_FAILED_:
          status = "Failed";
          break;
        }
        fprintf(filp, "<td> %s </td>", c->_casename_);
        fprintf(filp, "<td> %s </td>", status);
        fprintf(filp, "</tr>");
      }
      fprintf(filp, "</table></td>");
      fprintf(filp, "</tr>");
    }
    fprintf(filp, "</table>");

    if (failed > 0) {
      fprintf(filp, "Failed <%d failed, %d passed, %d todo, %d filtered, %d checks, %ld ms>\n", failed, passed, todo, filtered, checks, duration);
    } else {
      fprintf(filp, "Passed <%d passed, %d todo, %d filtered, %d cases, %d checks, %ld ms>\n", passed, todo, filtered, cases, checks, duration);
    }

    fprintf(filp, "</body>");
    fprintf(filp, "</html>");
    fclose(filp);
  }
};

class h2unit_listen_xml: public h2unit_listen
{
private:
  FILE *filp;

  void print_string(h2unit_list* s)
  {
    h2unit_list* p;
    h2unit_list_for_each(p, s) {
      h2unit_string* r = h2unit_list_entry(p, h2unit_string, link);
      fprintf(filp, "%s", r->data);
    }
  }
public:
  h2unit_listen_xml()
  {
  }
  virtual ~h2unit_listen_xml()
  {
  }
  void on_task_start()
  {
  }
  void on_task_endup(int failed, int passed, int todo, int filtered, int cases, int checks, long duration, h2unit_unit* unit_list)
  {
    h2unit_listen::on_task_endup(failed, passed, todo, filtered, cases, checks, duration, unit_list);
    filp = fopen("h2unit_junit.xml", "w");
    fprintf(filp, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n");
    fprintf(filp, "<testsuites>\n");
    for (h2unit_unit* p = unit_list; p; p = p->next) {
      fprintf(filp, "  <testsuite errors=\"0\" failures=\"%d\" hostname=\"localhost\" name=\"%s\" skipped=\"%d\" tests=\"%d\" time=\"%d\" timestamp=\"%s\">\n", p->case_failed, p->name, p->case_todo + p->case_filtered, p->case_count, 0, "");
      for (h2unit_case* c = p->cases; c; c = c->_next_) {
        const char* status;
        switch (c->_status_) {
        case h2unit_case::_TODOED_:
          status = "TODO";
          break;
        case h2unit_case::_FILTED_:
          status = "Filtered";
          break;
        case h2unit_case::_PASSED_:
          status = "Passed";
          break;
        case h2unit_case::_FAILED_:
          status = "Failed";
          break;
        }
        fprintf(filp, "    <testcase classname=\"%s\" name=\"%s\" status=\"%s\" time=\"%.3f\">\n", c->_unitname_, c->_casename_, status, (c->_endup_ - c->_start_) / 1000.0);
        if (c->_status_ == h2unit_case::_FAILED_) {
          fprintf(filp, "      <failure message=\"Failed at %s:%d\"></failure>\n", c->_checkfile_, c->_checkline_);
        }
        if (!h2unit_list_empty(&c->_errormsg_)) {
          fprintf(filp, "      <failure message=\"");
          print_string(&c->_errormsg_);
          fprintf(filp, "\"></failure>\n");
        }
        if (!h2unit_list_empty(&c->_expected_)) {
          fprintf(filp, "      <failure message=\"expected<");
          print_string(&c->_expected_);
          fprintf(filp, ">\"></failure>\n");
        }
        if (!h2unit_list_empty(&c->_unexpect_)) {
          fprintf(filp, "      <failure message=\"unexpect<");
          print_string(&c->_unexpect_);
          fprintf(filp, ">\"></failure>\n");
        }
        if (!h2unit_list_empty(&c->_actually_)) {
          fprintf(filp, "      <failure message=\"actually<");
          print_string(&c->_actually_);
          fprintf(filp, ">\"></failure>\n");
        }
        if (c->_addition_) {
          for (int i = 0; !h2unit_list_empty(&c->_addition_[i]); i++) {
            fprintf(filp, "      <failure message=\"");
            print_string(&c->_addition_[i]);
            fprintf(filp, "\"></failure>\n");
          }
        }
        fprintf(filp, "      <system-out></system-out><system-err></system-err>\n");
        fprintf(filp, "    </testcase>\n");
      }
      fprintf(filp, "  </testsuite>\n");
    }
    fprintf(filp, "</testsuites>\n");
    fclose(filp);
  }
};


class h2unit_task
{
public:
  int unit_count, case_count;
  int case_executed_count, checkpoint_count;
  int case_failed, case_passed, case_todo, case_filtered;
  h2unit_list blob_list;
  h2unit_list symb_list;
  h2unit_unit* unit_list;
  h2unit_case* case_chain;
  size_t limited;

  h2unit_listen_text text_listener;
  h2unit_listen_console console_listener;
  h2unit_listen_html html_listener;
  h2unit_listen_xml xml_listener;
  h2unit_listens listener;

  h2unit_task()
  {
    case_failed = case_passed = case_todo = case_filtered = 0;
    unit_count = case_count = case_executed_count = checkpoint_count = 0;
    case_chain = NULL;

    unit_list = NULL;
    h2unit_list_init(&blob_list);
    h2unit_list_init(&symb_list);
    limited = 0x7fffffff;

    listener.attach(&console_listener);
    listener.attach(&text_listener);
    listener.attach(&xml_listener);
    listener.attach(&html_listener);
  }

  static h2unit_task* O()
  {
    static h2unit_task _instance;
    return &_instance;
  }

  void build_symbols()
  {
#ifndef _WIN32
    char buf[512];
    char* line = buf;
    size_t len = sizeof(buf);
    int n;
    const char* symb_file = "h2unit_sym.txt";
    /**
     * TODO: for windows using dumpbin.exe or BFD (Binary File Descriptor Library)
     *
     * dumpbin.exe should be invoked from Visual Studio Command Prompt.
     * How to invoke dumpbin.exe with system() in code ?
     *
     * BFD is a very heavy library, it will break h2unit's principle : Light-Weight just like hydrogen !
     *
     * http://support.microsoft.com/kb/177429
     * http://sourceware.org/binutils/docs-2.23.1/bfd/index.html
     */
    sprintf(buf, "nm %s | c++filt > %s", __cfg._path, symb_file);
    system(buf);

    FILE* filp = fopen(symb_file, "r");
    if (filp == NULL) {
      return;
    }

    while ((n = getline(&line, &len, filp)) != -1) {
      char* t = strtok(line, " ");
      if (t && t[0] == '0' && strlen(t) > 4) {
        long a = strtol(t, NULL, 16);
        t = strtok(NULL, " ");
        if (t && tolower((int)t[0]) == 't' && strlen(t) == 1) {
          t = strtok(NULL, "\0");
          h2unit_symb* s = (h2unit_symb*) malloc(sizeof(h2unit_symb));
          memset(s, 0, sizeof(h2unit_symb));
          h2unit_list_init(&s->link);
          h2unit_symb_parse(s, t);
          s->addr = (void*)a;
          h2unit_list_add_tail(&s->link, &symb_list);
        }
      }
    }

    fclose(filp);
    sprintf(buf, "unlink %s", symb_file);
    system(buf);
#endif
  }

  void* get_symbol_address(const char* symb)
  {
    if (h2unit_list_empty(&symb_list)) {
      build_symbols();
    }
    h2unit_symb s;
    h2unit_symb_parse(&s, (char*)symb);

    h2unit_symb* u = NULL;
    double v = 0.0;

    h2unit_list* p;
    h2unit_list_for_each(p, &symb_list) {
      h2unit_symb* b = h2unit_list_entry(p, h2unit_symb, link);
      double c = h2unit_symb_compare(b, &s);
      if (c >= 1.0) {
        h2unit_symb_release(&s);
        return b->addr;
      }

      if (c > v) {
        v = c;
        u = b;
      }
    }
    h2unit_symb_release(&s);
    if (v > 0.5) {
      return u->addr;
    }
#if 0
    // dlsym() can get the dynamic library function address
    // it should include <dlfcn.h>, and link libdl.lib
    void* r = dlsym(RTLD_DEFAULT, symb); // RTLD_NEXT
    return r;
#endif
    return NULL;
  }

  void install_testcase(h2unit_case* testcase)
  {
    testcase->_chain_ = case_chain;
    case_chain = testcase;
    case_count++;

    h2unit_unit** p;
    for (p = &unit_list; *p; p = &(*p)->next) {
      if (strcmp((*p)->name, testcase->_unitname_) == 0) {
        break;
      }
    }
    if (*p == NULL) {
      *p = (h2unit_unit*) malloc(sizeof(h2unit_unit));
      memset(*p, 0, sizeof(h2unit_unit));
      (*p)->name = testcase->_unitname_;
      unit_count++;
    }

    h2unit_case** c;
    for (c = &(*p)->cases; *c; c = &(*c)->_next_) {
      if ((*c)->_caseline_ > testcase->_caseline_) {
        break;
      }
    }
    testcase->_next_ = *c;
    *c = testcase;
  }

  void random_sequence()
  {
    h2unit_case* p = case_chain;
    case_chain = NULL;
    srandom(__milliseconds());
    for (int i = 1; p; i++) {
      h2unit_case* n = p->_chain_;

      int rpos = random() % i;
      h2unit_case** ip = &case_chain;
      for (int j = 0; j < rpos; j++) {
        ip = &((*ip)->_chain_);
      }
      p->_chain_ = *ip;
      *ip = p;

      p = n;
    }
  }

  void run()
  {
    long start = __milliseconds();
    if (__cfg._random) random_sequence();
    listener.on_task_start();
    for (h2unit_case* p = case_chain; (h2unit_case::_current_ = p); p = p->_chain_) {
      listener.on_case_start();
      p->_execute_();
      case_executed_count += 1;
      checkpoint_count += p->_checkcount_;

      listener.on_case_endup();

      switch (p->_status_) {
      case h2unit_case::_TODOED_:
        case_todo++;
        break;
      case h2unit_case::_FILTED_:
        case_filtered++;
        break;
      case h2unit_case::_PASSED_:
        case_passed++;
        break;
      case h2unit_case::_FAILED_:
        case_failed++;
        break;
      }
    }
    listener.on_task_endup(case_failed, case_passed, case_todo, case_filtered, case_count, checkpoint_count, __milliseconds() - start, unit_list);
  }

  h2unit_blob* get_blob(void* ptr)
  {
    h2unit_list* p;
    h2unit_list_for_each(p, &blob_list) {
      h2unit_blob* b = h2unit_list_entry(p, h2unit_blob, queue);
      if (b->ptr == ptr) return b;
    }
    return NULL;
  }

  h2unit_blob* add_blob(void* old, size_t size, size_t alignment, unsigned char c, const char* file, int line)
  {
    h2unit_blob* old_blob = get_blob(old);
    size_t old_size = old_blob ? old_blob->size : 0;

    if (size > limited + old_size) return NULL;

    h2unit_blob* new_blob = h2unit_blob_new(size, alignment, c, file, line);

    h2unit_list_add_head(&new_blob->queue, &blob_list);

    if (h2unit_case::_current_) {
      h2unit_case::_current_->_blob_add_(&new_blob->stack);
    }

    if (old_blob) {
      memcpy(new_blob->ptr, old_blob->ptr, old_size);
      del_blob(old_blob);
    }
    limited -= size;

    return new_blob;
  }

  void del_blob(h2unit_blob* blob)
  {
    limited += blob->size;
    h2unit_list_del(&blob->stack);
    h2unit_list_del(&blob->queue);
    h2unit_blob_del(blob);
  }
};

h2unit_auto::h2unit_auto(const char* file, int line)
{
  done = false;
  h2unit_case::_current_->_leak_push_(file, line);
}

h2unit_auto::~h2unit_auto()
{
  if (!h2unit_case::_current_->_leak_pop_()) {
    longjmp(__h2unit_jmp_buf, 1);
  }
}

h2unit_case* h2unit_case::_current_ = NULL;

h2unit_case::h2unit_case()
{
}

h2unit_case::~h2unit_case()
{
}

void h2unit_case::_init_(const char* unitname, const char* casename, bool ignored, const char* file, int line)
{
  _status_ = _INITED_;
  _unitname_ = unitname;
  _casename_ = casename;
  _casefile_ = file;
  _caseline_ = line;

  _checkfile_ = file;
  _checkline_ = line;
  _checkcount_ = 0;

  h2unit_list_init(&_errormsg_);
  h2unit_list_init(&_expected_);
  h2unit_list_init(&_unexpect_);
  h2unit_list_init(&_actually_);
  _addition_ = NULL;

  h2unit_list_init(&_leak_stack_);
  h2unit_list_init(&_stub_list_);

  if (ignored) _status_ = _TODOED_;

  h2unit_task::O()->install_testcase(this);
}

void h2unit_case::_prev_setup_()
{
  _leak_push_(NULL, 0);
}

void h2unit_case::_post_setup_()
{
}

void h2unit_case::_prev_teardown_()
{
}

void h2unit_case::_post_teardown_()
{
  /* balance test environment automatically */
  _limit_(0x7fffffff);

  h2unit_list* p, *t;
  h2unit_list_for_each_safe(p, t, &_stub_list_) {
    h2unit_list_del(p);
    h2unit_stub* stub = h2unit_list_entry(p, h2unit_stub, link);
    h2unit_stub_del(stub);
  }

  /* memory leak detection */
  _leak_pop_();
}

void h2unit_case::setup()
{
}

void h2unit_case::teardown()
{
}

void h2unit_case::_execute_()
{
  _start_ = __milliseconds();
  if (__cfg._include != NULL && (!__wildcard_match(__cfg._include, (char*) _unitname_) && !__wildcard_match(__cfg._include, (char*) _casename_))) {
    _status_ = _FILTED_;
    return;
  }
  if (__cfg._exclude != NULL && (__wildcard_match(__cfg._exclude, (char*) _unitname_) || __wildcard_match(__cfg._exclude, (char*) _casename_))) {
    _status_ = _FILTED_;
    return;
  }

  if (_status_ != _TODOED_) {
    _status_ = _PASSED_;
    _prev_setup_();
    setup();
    _post_setup_();
    if (!setjmp(__h2unit_jmp_buf)) {
      _testcase_();
    } else {
      _status_ = _FAILED_;
    }
    _prev_teardown_();
    teardown();
    _post_teardown_();
  }

  _endup_ = __milliseconds();
}

void h2unit_case::_limit_(unsigned long bytes)
{
  h2unit_task::O()->limited = bytes;
}

void h2unit_case::_leak_push_(const char* file, int line)
{
  h2unit_leak* leak = (h2unit_leak*) malloc(sizeof(h2unit_leak));
  memset(leak, 0, sizeof(h2unit_leak));
  leak->file = file;
  leak->line = line;
  h2unit_list_init(&leak->stack);
  h2unit_list_init(&leak->blobs);
  h2unit_list_add_head(&leak->stack, &_leak_stack_);
}

void h2unit_case::_blob_add_(h2unit_list* blob)
{
  h2unit_list* head = h2unit_list_get_head(&_leak_stack_);
  h2unit_leak* leak = h2unit_list_entry(head, h2unit_leak, stack);
  h2unit_list_add_tail(blob, &leak->blobs);
}

bool h2unit_case::_leak_pop_()
{
  h2unit_list* head = h2unit_list_get_head(&_leak_stack_);
  h2unit_list_del(head);

  h2unit_leak* leak = h2unit_list_entry(head, h2unit_leak, stack);
  if (h2unit_list_empty(&leak->blobs)) {
    free(leak);
    return true;
  }

  if (_status_ == _FAILED_) { /* other failure already happen, ignore memory leak failure */
    return true;
  }

  _status_ = _FAILED_;

  int count = 0;
  h2unit_list* p;
  h2unit_list_for_each(p, &leak->blobs) {
    count++;
  }

  _addition_ = (h2unit_list*) malloc((count + 1) * sizeof(h2unit_list));

  for (int i = 0; i < count + 1; i++) {
    h2unit_list_init(&_addition_[i]);
  }
  int j = 0;
  size_t leaked = 0;
  h2unit_list_for_each(p, &leak->blobs) {
    h2unit_blob* b = h2unit_list_entry(p, h2unit_blob, stack);
    _vmsg_(&_addition_[j], "bold,red", "Leaked %d bytes", b->size);
    _vmsg_(&_addition_[j], "", " at %s:%d", b->file, b->line);
    j++;
    leaked += b->size;
  }

  if (leak->file == NULL) {
    _vmsg_(&_errormsg_, "bold,red", "Memory Leaked %d bytes in case totally", leaked);
  } else {
    _vmsg_(&_errormsg_, "bold,red", "Memory Leaked %d bytes in block", leaked);
    _vmsg_(&_errormsg_, "", " at %s:%d", leak->file, leak->line);
  }

  return false;
}

void h2unit_case::_vmsg_(h2unit_list* typed, const char *style, const char* format, ...)
{
  h2unit_string* p = (h2unit_string*) malloc(sizeof(h2unit_string));
  memset(p, 0, sizeof(h2unit_string));
  h2unit_list_init(&p->link);
  h2unit_list_add_tail(&p->link, typed);

  va_list args;
  va_start(args, format);
  static char t[1024 * 8];
  int sz = vsprintf(t, format, args);
  va_end(args);

  p->style = style;
  p->data = (char*) malloc(sz + 1);

  va_start(args, format);
  vsprintf(p->data, format, args);
  va_end(args);
}

void* h2unit_case::_addr_(const char* native, const char* native_name, const char* fake_name)
{
  void *address = h2unit_task::O()->get_symbol_address(native);
  if (address == NULL) {
    _vmsg_(&_errormsg_, "", "H2STUB(");
    _vmsg_(&_errormsg_, "bold,red", "%s", native_name);
    _vmsg_(&_errormsg_, "", " <-- ");
    _vmsg_(&_errormsg_, "bold,red", "%s", fake_name);
    _vmsg_(&_errormsg_, "", ")");

    char b[256], *p, *q;
    sprintf(b, "%s", native_name);
    for (p = b; *p == '\"'; p++);
    for (q = p + strlen(p) - 1; *q == '\"'; q--) *q = '\0';

    _vmsg_(&_errormsg_, "bold,purple", " %s not found, try H2STUB(&%s, %s);", native_name, p, fake_name);

    longjmp(__h2unit_jmp_buf, 1);
  }
  return address;
}

void h2unit_case::_stub_(void* native, void* fake, const char* native_name, const char* fake_name)
{
  h2unit_stub* stub = NULL;
  h2unit_list* p;
  h2unit_list_for_each(p, &_stub_list_) {
    h2unit_stub* s = h2unit_list_entry(p, h2unit_stub, link);
    if (s->native == native) {
      stub = s;
      break;
    }
  }
  if (!stub) {
    stub = h2unit_stub_new();
    h2unit_list_add_head(&stub->link, &_stub_list_);
    char* reason = h2unit_stub_save(stub, native);
    if (reason != NULL) {
      _vmsg_(&_errormsg_, "", "H2STUB(");
      _vmsg_(&_errormsg_, "bold,red", "%s", native_name);
      _vmsg_(&_errormsg_, "", " <-- ");
      _vmsg_(&_errormsg_, "bold,red", "%s", fake_name);
      _vmsg_(&_errormsg_, "", ")");
      _vmsg_(&_errormsg_, "bold,red", " %s", reason);

      longjmp(__h2unit_jmp_buf, 1);
    }
  }
  h2unit_stub_set(stub, fake);
}

void h2unit_case::_enter_check_(const char* file, int line)
{
  _checkfile_ = file;
  _checkline_ = line;
  _checkcount_++;
}

void h2unit_case::_check_equal_boolean_(bool result)
{
  if (!result) {
    _vmsg_(&_expected_, "bold,red", "true");
    _vmsg_(&_actually_, "bold,red", "false");

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_equal_math_(long double expected, long double actually)
{
  long double delta = expected - actually;
  if (delta < 0) delta = -delta;
  if (delta > 0.00001) { /* 0.00001 is epsilon value */

    long double fraction = expected - static_cast<unsigned long long int>(expected);
    if (fraction == .0) {
#ifdef _WIN32
      _vmsg_(&_expected_, "bold,red", "%Lg 0x%I64x", expected, static_cast<unsigned long long int>(expected));
      _vmsg_(&_actually_, "bold,red", "%Lg 0x%I64x", actually, static_cast<unsigned long long int>(actually));
#else
      _vmsg_(&_expected_, "bold,red", "%Lg 0x%llx", expected, static_cast<unsigned long long int>(expected));
      _vmsg_(&_actually_, "bold,red", "%Lg 0x%llx", actually, static_cast<unsigned long long int>(actually));
#endif
    } else {
      _vmsg_(&_expected_, "bold,red", "%Lg", expected);
      _vmsg_(&_actually_, "bold,red", "%Lg", actually);
    }

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_equal_math_(void* expected, void* actually)
{
  if (expected != actually) {
    _vmsg_(&_expected_, "bold,red", "%p", expected);
    _vmsg_(&_actually_, "bold,red", "%p", actually);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_unequal_math_(long double unexpect, long double actually)
{
  long double delta = unexpect - actually;
  if (delta < 0) delta = -delta;
  if (delta < 0.00001) {

    long double fraction = unexpect - static_cast<unsigned long long int>(unexpect);
    if (fraction == .0) {
#ifdef _WIN32
      _vmsg_(&_unexpect_, "bold,red", "%Lg 0x%I64x", unexpect, static_cast<unsigned long long int>(unexpect));
      _vmsg_(&_actually_, "bold,red", "%Lg 0x%I64x", actually, static_cast<unsigned long long int>(actually));
#else
      _vmsg_(&_unexpect_, "bold,red", "%Lg 0x%llx", unexpect, static_cast<unsigned long long int>(unexpect));
      _vmsg_(&_actually_, "bold,red", "%Lg 0x%llx", actually, static_cast<unsigned long long int>(actually));
#endif
    } else {
      _vmsg_(&_unexpect_, "bold,red", "%Lg", unexpect);
      _vmsg_(&_actually_, "bold,red", "%Lg", actually);
    }

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_unequal_math_(void* unexpect, void* actually)
{
  if (unexpect == actually) {

    _vmsg_(&_unexpect_, "bold,red", "%p", unexpect);
    _vmsg_(&_actually_, "bold,red", "%p", actually);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_equal_range_(double from, double to, double actually)
{
  if (actually < from - 0.00001 || to + 0.00001 < actually) {
    _vmsg_(&_expected_, "bold,red", "[%g ~ %g]", from, to);
    _vmsg_(&_actually_, "bold,red", "%g", actually);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_unequal_range_(double from, double to, double actually)
{
  if (from - 0.00001 <=  actually && actually <= to + 0.00001) {
    _vmsg_(&_unexpect_, "bold,red", "[%g ~ %g]", from, to);
    _vmsg_(&_actually_, "bold,red", "%g", actually);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_equal_inset_(double *inset, int count, double actually)
{
  for (int i = 0; i < count; i++) {
    double delta = inset[i] - actually;
    if (delta < 0) delta = -delta;
    if (delta < 0.00001) {
      return;
    }

  }
  if (count > 0) {
    _vmsg_(&_expected_, "bold,red", "{%g, ...}", inset[0]);
  } else {
    _vmsg_(&_expected_, "bold,red", "{}");
  }
  _vmsg_(&_actually_, "bold,red", "%g", actually);

  longjmp(__h2unit_jmp_buf, 1);
}

void h2unit_case::_check_unequal_inset_(double *inset, int count, double actually)
{
  for (int i = 0; i < count; i++) {
    double delta = inset[i] - actually;
    if (delta < 0) delta = -delta;
    if (delta < 0.00001) {
      if (count > 0) {
        _vmsg_(&_unexpect_, "bold,red", "{%g, ...}", inset[0]);
      } else {
        _vmsg_(&_unexpect_, "bold,red", "{}");
      }
      _vmsg_(&_actually_, "bold,red", "%g", actually);

      longjmp(__h2unit_jmp_buf, 1);
    }
  }
}

void h2unit_case::_check_equal_strcmp_(char* expected, char* actually)
{
  if (strcmp(expected, actually) != 0) {
    int actually_length = (int)strlen(actually);
    int expected_length = (int)strlen(expected);

    _vmsg_(&_expected_, "", "");
    _vmsg_(&_actually_, "", "");

    for (int i = 0; i < expected_length; i++) {
      _vmsg_(&_expected_, i <= actually_length && expected[i] == actually[i] ? "green" : "bold,red", "%c", expected[i]);
    }
    for (int j = 0; j < actually_length; j++) {
      _vmsg_(&_actually_, j <= expected_length && actually[j] == expected[j] ? "green" : "bold,red", "%c", actually[j]);
    }

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_unequal_strcmp_(char* unexpect, char* actually)
{
  if (strcmp(unexpect, actually) == 0) {
    _vmsg_(&_unexpect_, "bold,red", "%s", unexpect);
    _vmsg_(&_actually_, "bold,red", "%s", actually);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_equal_strcmp_nocase_(char* expected, char* actually)
{
  if (strcasecmp(expected, actually) != 0) {
    int actually_length = (int)strlen(actually);
    int expected_length = (int)strlen(expected);

    _vmsg_(&_expected_, "", "");
    _vmsg_(&_actually_, "", "");

    for (int i = 0; i < expected_length; i++) {
      _vmsg_(&_expected_, i <= actually_length && tolower((int) expected[i]) == tolower((int) actually[i]) ? "green" : "bold,red", "%c", expected[i]);
    }
    for (int j = 0; j < actually_length; j++) {
      _vmsg_(&_actually_, j <= expected_length && tolower((int) actually[j]) == tolower((int) expected[j]) ? "green" : "bold,red", "%c", actually[j]);
    }

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_equal_json_(char* expected, char* actually)
{
  char *l = NULL, *r = NULL;
  if (cJSON::cJSON_Compare(expected, actually, &l, &r) != 0) {
    if (l && strlen(l)) {
      expected = l;
    }
    if (r && strlen(r)) {
      actually = r;
    }
    int actually_length = (int)strlen(actually);
    int expected_length = (int)strlen(expected);

    _vmsg_(&_expected_, "", "");
    _vmsg_(&_actually_, "", "");

    for (int i = 0; i < expected_length; i++) {
      bool c = false;
      if (expected[i] == '>') {
        int n = 1;
        for (int l = i - 1; l >= 0; l--) {if (expected[l] == '>') n++; else break;}
        for (int r = i + 1; r < expected_length; r++) {if (expected[r] == '>') n++; else break;}
        c = n == 7;
      }
      _vmsg_(&_expected_, c ? "bold,red" : "green", "%c", expected[i]);
    }
    for (int j = 0; j < actually_length; j++) {
      bool c = false;
      if (actually[j] == '<') {
        int n = 1;
        for (int l = j - 1; l >= 0; l--) {if (actually[l] == '<') n++; else break;}
        for (int r = j + 1; r < actually_length; r++) {if (actually[r] == '<') n++; else break;}
        c = n == 7;
      }
      _vmsg_(&_actually_, c ? "bold,red" : "green", "%c", actually[j]);
    }    
    if (l) free(l);
    if (r) free(r);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_unequal_json_(char* unexpect, char* actually)
{
  char *l = NULL, *r = NULL;
  if (cJSON::cJSON_Compare(unexpect, actually, &l, &r) == 0) {
    if (l && strlen(l)) {
      unexpect = l;
    }
    if (r && strlen(r)) {
      actually = r;
    }
    _vmsg_(&_unexpect_, "bold,red", "%s", unexpect);
    _vmsg_(&_actually_, "bold,red", "%s", actually);

    if (l) free(l);
    if (r) free(r);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_equal_wildcard_(char* express, char* actually)
{
  if (!__wildcard_match(express, actually)) {
    _vmsg_(&_expected_, "bold,red", "%s", express);
    _vmsg_(&_actually_, "bold,red", "%s", actually);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_unequal_wildcard_(char* express, char* actually)
{
  if (__wildcard_match(express, actually)) {
    _vmsg_(&_unexpect_, "bold,red", "%s", express);
    _vmsg_(&_actually_, "bold,red", "%s", actually);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_equal_regex_(char* express, char* actually)
{
  if (SLRE::slre_match(express, actually, strlen(actually), NULL, 0, 0) < 0) {
    _vmsg_(&_expected_, "bold,red", "%s", express);
    _vmsg_(&_actually_, "bold,red", "%s", actually);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_unequal_regex_(char* express, char* actually)
{
  if (SLRE::slre_match(express, actually, strlen(actually), NULL, 0, 0) >= 0) {
    _vmsg_(&_unexpect_, "bold,red", "%s", express);
    _vmsg_(&_actually_, "bold,red", "%s", actually);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_equal_memcmp_(unsigned char* expected, unsigned char* actually, int length)
{
  if (memcmp(expected, actually, length) != 0) {
    for (int i = 0; i < length; i++) {
      _vmsg_(&_expected_, expected[i] == actually[i] ? "green" : "bold,red", i % 16 < 8 ? "%02X " : " %02X", expected[i]);
      _vmsg_(&_actually_, actually[i] == expected[i] ? "green" : "bold,red", i % 16 < 8 ? "%02X " : " %02X", actually[i]);
    }

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_case::_check_catch_(const char* expected, const char* actually, const char* exceptype)
{
  if (expected != actually) {
    _vmsg_(&_expected_, "bold,red", "%s %s", expected, exceptype);
    _vmsg_(&_actually_, "bold,red", "%s %s", actually, exceptype);

    longjmp(__h2unit_jmp_buf, 1);
  }
}

void h2unit_assert(int condition, const char* file, int line)
{
  if (h2unit_case::_current_) {
    h2unit_case::_current_->_enter_check_(file, line);
    h2unit_case::_current_->_check_equal_boolean_((bool)condition);
  }
}

void* h2unit_alloc(void* ptr, size_t size, size_t alignment, unsigned char c, const char* file, int line)
{
  if (size == 0) {
    h2unit_free(ptr, file, line);
    return NULL;
  }
  h2unit_blob* b = h2unit_task::O()->add_blob(ptr, size, alignment, c, file, line);
  return b ? b->ptr : NULL;
}

void h2unit_free(void* ptr, const char* file, int line)
{
  h2unit_blob* b = h2unit_task::O()->get_blob(ptr);
  if (b) h2unit_task::O()->del_blob(b);
}

int h2unit_posix_memalign(void** ptr, size_t alignment, size_t size, const char* file, int line)
{
  alignment = 4;
  h2unit_blob* b = h2unit_task::O()->add_blob(NULL, size, alignment, 0xED, file, line);
  if (b) {
    *ptr = b->ptr;
    return 0;
  }
#ifndef ENOMEM
#define ENOMEM -1
#endif
  return ENOMEM;
}

char* h2unit_strdup(const char* s, const char* file, int line)
{
  size_t size = strlen(s) + 1;
  h2unit_blob* b = h2unit_task::O()->add_blob(NULL, size, 4, 0xED, file, line);
  if (b) {
    memcpy(b->ptr, (const void*)s, size);
  }
  return (char*) (b ? b->ptr : NULL);
}

char* h2unit_strndup(const char* s, size_t n, const char* file, int line)
{
  size_t size = strlen(s);
  size = (size > n ? n : size) + 1;
  h2unit_blob* b = h2unit_task::O()->add_blob(NULL, size, 4, 0xED, file, line);
  if (b) {
    memcpy(b->ptr, (const void*)s, size - 1);
    ((char*)b->ptr)[size - 1] = '\0';
  }
  return (char*) (b ? b->ptr : NULL);
}

void* operator new(size_t size, const char* file, int line)
{
  return h2unit_alloc(NULL, size, 4, 0x0, file, line);
}

void* operator new[](size_t size, const char* file, int line)
{
  return h2unit_alloc(NULL, size, 4, 0x0, file, line);
}

void operator delete(void* object)
{
  h2unit_free(object, "", 0);
}

void operator delete[](void* object)
{
  h2unit_free(object, "", 0);
}

int h2unit_main(int argc, char** argv)
{
  __cfg._path = argv[0];
  if (argc > 1) {
    if (strstr(argv[1], "v")) __cfg._verbose = true;
    if (strstr(argv[1], "b")) __cfg._colored = false;
    if (strstr(argv[1], "r")) __cfg._random = true;
    if (strstr(argv[1], "i")) __cfg._include = argv[2];
    if (strstr(argv[1], "x")) __cfg._exclude = argv[2];
  }
  h2unit_task::O()->run();
  return 0;
}

int main(int argc, char** argv)
{
  return h2unit_main(argc, argv);
}

