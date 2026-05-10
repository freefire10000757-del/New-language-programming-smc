/*
 * ╔══════════════════════════════════════════════════════════╗
 * ║           Shield (SM) Virtual Machine v1.0              ║
 * ║         الآلة الافتراضية للغة Shield — بلغة C           ║
 * ║     بدون Python — مستقل 100% — يعمل على Termux          ║
 * ╚══════════════════════════════════════════════════════════╝
 *
 * البناء في Termux:
 *   clang shield_vm.c -o shield-vm -lm
 *   ./shield-vm file.smbc
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* ─────────────────────────────────────────
   OPCODES — رموز التعليمات
   ───────────────────────────────────────── */

#define OP_PUSH_INT    0x01
#define OP_PUSH_FLOAT  0x02
#define OP_PUSH_STR    0x03
#define OP_PUSH_BOOL   0x04
#define OP_PUSH_NULL   0x05
#define OP_POP         0x06
#define OP_DUP         0x07

#define OP_LOAD        0x10
#define OP_STORE       0x11
#define OP_LOAD_CONST  0x12

#define OP_ADD         0x20
#define OP_SUB         0x21
#define OP_MUL         0x22
#define OP_DIV         0x23
#define OP_MOD         0x24
#define OP_NEG         0x25

#define OP_EQ          0x30
#define OP_NEQ         0x31
#define OP_LT          0x32
#define OP_GT          0x33
#define OP_LE          0x34
#define OP_GE          0x35

#define OP_AND         0x40
#define OP_OR          0x41
#define OP_NOT         0x42

#define OP_JMP         0x50
#define OP_JMP_IF      0x51
#define OP_JMP_IFNOT   0x52

#define OP_CALL        0x60
#define OP_RETURN      0x61
#define OP_MAKE_FN     0x62

#define OP_MAKE_LIST   0x70
#define OP_MAKE_DICT   0x71
#define OP_INDEX_GET   0x72
#define OP_INDEX_SET   0x73
#define OP_ATTR_GET    0x74
#define OP_ATTR_SET    0x75

#define OP_PRINT       0x80
#define OP_INPUT       0x81

#define OP_HALT        0xFF

/* ─────────────────────────────────────────
   VALUE TYPES — أنواع القيم
   ───────────────────────────────────────── */

typedef enum {
    VAL_INT = 0,
    VAL_FLOAT,
    VAL_STR,
    VAL_BOOL,
    VAL_NULL,
    VAL_LIST,
    VAL_DICT,
    VAL_FN,
} ValType;

/* Forward declarations */
struct Value;
struct List;
struct Dict;
struct Chunk;
struct Frame;

typedef struct Value Value;
typedef struct List  List;
typedef struct Dict  Dict;
typedef struct Chunk Chunk;
typedef struct Frame Frame;

/* القائمة الديناميكية */
struct List {
    Value  **items;
    int      count;
    int      cap;
    int      refs;  /* reference counting */
};

/* زوج المفتاح/القيمة في القاموس */
typedef struct {
    Value *key;
    Value *val;
} DictEntry;

struct Dict {
    DictEntry *entries;
    int        count;
    int        cap;
    int        refs;
};

/* مقطع الكود (دالة أو البرنامج الرئيسي) */
struct Chunk {
    char    *name;
    uint8_t *code;
    int      code_len;
    Value  **consts;
    int      const_count;
    char   **names;
    int      name_count;
};

/* قيمة الدالة */
typedef struct {
    Chunk  *chunk;
    int     refs;
} FnVal;

/* القيمة العامة */
struct Value {
    ValType type;
    union {
        int64_t  i;
        double   f;
        char    *s;
        int      b;   /* bool */
        List    *list;
        Dict    *dict;
        FnVal   *fn;
    } as;
};

/* ─────────────────────────────────────────
   MEMORY — إدارة الذاكرة
   ───────────────────────────────────────── */

static Value *alloc_val(ValType t) {
    Value *v = (Value*)calloc(1, sizeof(Value));
    if (!v) { fprintf(stderr, "[SM] نفاد الذاكرة\n"); exit(1); }
    v->type = t;
    return v;
}

static Value *make_int(int64_t n) {
    Value *v = alloc_val(VAL_INT);
    v->as.i  = n;
    return v;
}

static Value *make_float(double f) {
    Value *v = alloc_val(VAL_FLOAT);
    v->as.f  = f;
    return v;
}

static Value *make_str(const char *s) {
    Value *v = alloc_val(VAL_STR);
    v->as.s  = strdup(s);
    return v;
}

static Value *make_bool(int b) {
    Value *v = alloc_val(VAL_BOOL);
    v->as.b  = b ? 1 : 0;
    return v;
}

static Value *make_null(void) {
    return alloc_val(VAL_NULL);
}

static Value *make_list(void) {
    Value *v    = alloc_val(VAL_LIST);
    List  *lst  = (List*)calloc(1, sizeof(List));
    lst->cap    = 8;
    lst->items  = (Value**)malloc(8 * sizeof(Value*));
    lst->refs   = 1;
    v->as.list  = lst;
    return v;
}

static Value *make_dict(void) {
    Value *v    = alloc_val(VAL_DICT);
    Dict  *d    = (Dict*)calloc(1, sizeof(Dict));
    d->cap      = 8;
    d->entries  = (DictEntry*)malloc(8 * sizeof(DictEntry));
    d->refs     = 1;
    v->as.dict  = d;
    return v;
}

static void list_push(List *lst, Value *item) {
    if (lst->count >= lst->cap) {
        lst->cap  *= 2;
        lst->items = (Value**)realloc(lst->items, lst->cap * sizeof(Value*));
    }
    lst->items[lst->count++] = item;
}

static void dict_set(Dict *d, Value *key, Value *val) {
    /* linear search — كافٍ للمرحلة الأولى */
    for (int i = 0; i < d->count; i++) {
        Value *k = d->entries[i].key;
        if (k->type == key->type && k->type == VAL_STR &&
            strcmp(k->as.s, key->as.s) == 0) {
            d->entries[i].val = val;
            return;
        }
    }
    if (d->count >= d->cap) {
        d->cap    *= 2;
        d->entries = (DictEntry*)realloc(d->entries, d->cap * sizeof(DictEntry));
    }
    d->entries[d->count].key = key;
    d->entries[d->count].val = val;
    d->count++;
}

static Value *dict_get(Dict *d, Value *key) {
    for (int i = 0; i < d->count; i++) {
        Value *k = d->entries[i].key;
        if (k->type == VAL_STR && key->type == VAL_STR &&
            strcmp(k->as.s, key->as.s) == 0)
            return d->entries[i].val;
        if (k->type == VAL_INT && key->type == VAL_INT &&
            k->as.i == key->as.i)
            return d->entries[i].val;
    }
    return NULL;
}

/* ─────────────────────────────────────────
   DISPLAY — عرض القيم
   ───────────────────────────────────────── */

static void print_val(Value *v) {
    if (!v) { printf("null"); return; }
    switch (v->type) {
        case VAL_INT:   printf("%lld", (long long)v->as.i); break;
        case VAL_FLOAT: {
            /* إزالة الصفر غير الضروري */
            if (v->as.f == (int64_t)v->as.f)
                printf("%.1f", v->as.f);
            else
                printf("%g", v->as.f);
            break;
        }
        case VAL_STR:   printf("%s", v->as.s); break;
        case VAL_BOOL:  printf("%s", v->as.b ? "true" : "false"); break;
        case VAL_NULL:  printf("null"); break;
        case VAL_LIST: {
            List *lst = v->as.list;
            printf("[");
            for (int i = 0; i < lst->count; i++) {
                print_val(lst->items[i]);
                if (i < lst->count - 1) printf(", ");
            }
            printf("]");
            break;
        }
        case VAL_DICT: {
            Dict *d = v->as.dict;
            printf("{");
            for (int i = 0; i < d->count; i++) {
                print_val(d->entries[i].key);
                printf(": ");
                print_val(d->entries[i].val);
                if (i < d->count - 1) printf(", ");
            }
            printf("}");
            break;
        }
        case VAL_FN:
            printf("<fn %s>", v->as.fn->chunk->name);
            break;
    }
}

static int val_truthy(Value *v) {
    if (!v) return 0;
    switch (v->type) {
        case VAL_BOOL:  return v->as.b;
        case VAL_INT:   return v->as.i != 0;
        case VAL_FLOAT: return v->as.f != 0.0;
        case VAL_STR:   return v->as.s && v->as.s[0] != '\0';
        case VAL_NULL:  return 0;
        case VAL_LIST:  return v->as.list->count > 0;
        default:        return 1;
    }
}

/* مقارنة قيمتين */
static int val_eq(Value *a, Value *b) {
    if (a->type != b->type) {
        /* int vs float */
        if (a->type == VAL_INT && b->type == VAL_FLOAT)
            return (double)a->as.i == b->as.f;
        if (a->type == VAL_FLOAT && b->type == VAL_INT)
            return a->as.f == (double)b->as.i;
        return 0;
    }
    switch (a->type) {
        case VAL_INT:  return a->as.i == b->as.i;
        case VAL_FLOAT:return a->as.f == b->as.f;
        case VAL_STR:  return strcmp(a->as.s, b->as.s) == 0;
        case VAL_BOOL: return a->as.b == b->as.b;
        case VAL_NULL: return 1;
        default:       return a == b;
    }
}

static double val_to_dbl(Value *v) {
    if (v->type == VAL_INT)   return (double)v->as.i;
    if (v->type == VAL_FLOAT) return v->as.f;
    fprintf(stderr, "[SM] نوع غير رقمي\n"); exit(1);
}

/* ─────────────────────────────────────────
   STACK — المكدس
   ───────────────────────────────────────── */

#define STACK_MAX 1024
#define VARS_MAX  512
#define FRAMES_MAX 64

typedef struct {
    Value  *stack[STACK_MAX];
    int     sp;          /* stack pointer */
    Value  *vars[VARS_MAX];  /* variables */
} VMState;

static VMState *g_vm = NULL;

static void vm_push(Value *v) {
    if (g_vm->sp >= STACK_MAX - 1) {
        fprintf(stderr, "[SM] فيضان المكدس\n"); exit(1);
    }
    g_vm->stack[g_vm->sp++] = v;
}

static Value *vm_pop(void) {
    if (g_vm->sp <= 0) {
        fprintf(stderr, "[SM] المكدس فارغ\n"); exit(1);
    }
    return g_vm->stack[--g_vm->sp];
}

static Value *vm_peek(void) {
    return g_vm->sp > 0 ? g_vm->stack[g_vm->sp - 1] : NULL;
}

/* ─────────────────────────────────────────
   READ BYTECODE FILE — قراءة الملف
   ───────────────────────────────────────── */

static uint8_t *g_file_data = NULL;
static size_t   g_file_size = 0;

static uint32_t read_u32(size_t *pos) {
    uint8_t *p = g_file_data + *pos;
    uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                 ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
    *pos += 4;
    return v;
}

static int64_t read_i64(size_t *pos) {
    uint8_t *p = g_file_data + *pos;
    int64_t  v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | p[i];
    *pos += 8;
    return v;
}

static double read_dbl(size_t *pos) {
    /* IEEE 754 big-endian */
    uint8_t buf[8];
    for (int i = 0; i < 8; i++)
        buf[7-i] = g_file_data[*pos + i];
    *pos += 8;
    double v;
    memcpy(&v, buf, 8);
    return v;
}

static char *read_str(size_t *pos, uint32_t len) {
    char *s = (char*)malloc(len + 1);
    memcpy(s, g_file_data + *pos, len);
    s[len] = '\0';
    *pos  += len;
    return s;
}

/* ─────────────────────────────────────────
   CHUNK LOADER — تحميل مقاطع الكود
   ───────────────────────────────────────── */

#define MAX_CHUNKS 128
static Chunk *g_chunks[MAX_CHUNKS];
static int    g_chunk_count = 0;

static Chunk *load_chunk(size_t *pos) {
    Chunk *c = (Chunk*)calloc(1, sizeof(Chunk));

    /* name */
    uint32_t nlen = read_u32(pos);
    c->name       = read_str(pos, nlen);

    /* consts */
    uint32_t ccount = read_u32(pos);
    c->consts       = (Value**)malloc(ccount * sizeof(Value*));
    c->const_count  = ccount;
    for (uint32_t i = 0; i < ccount; i++) {
        char tag = (char)g_file_data[(*pos)++];
        if (tag == 's') {
            uint32_t slen = read_u32(pos);
            char    *s    = read_str(pos, slen);
            c->consts[i]  = make_str(s);
            free(s);
        } else if (tag == 'i') {
            int64_t n    = read_i64(pos);
            c->consts[i] = make_int(n);
        } else if (tag == 'f') {
            double d     = read_dbl(pos);
            c->consts[i] = make_float(d);
        } else if (tag == 'n') {
            c->consts[i] = make_null();
        } else if (tag == 'b') {
            int b        = (int)g_file_data[(*pos)++];
            c->consts[i] = make_bool(b);
        } else {
            fprintf(stderr, "[SM] نوع ثابت غير معروف: %c\n", tag);
            exit(1);
        }
    }

    /* names */
    uint32_t ncount = read_u32(pos);
    c->names        = (char**)malloc(ncount * sizeof(char*));
    c->name_count   = ncount;
    for (uint32_t i = 0; i < ncount; i++) {
        uint32_t nl  = read_u32(pos);
        c->names[i]  = read_str(pos, nl);
    }

    /* code */
    uint32_t clen = read_u32(pos);
    c->code       = (uint8_t*)malloc(clen);
    c->code_len   = clen;
    memcpy(c->code, g_file_data + *pos, clen);
    *pos += clen;

    return c;
}

static Chunk *find_chunk(const char *name) {
    for (int i = 0; i < g_chunk_count; i++)
        if (strcmp(g_chunks[i]->name, name) == 0)
            return g_chunks[i];
    return NULL;
}

/* ─────────────────────────────────────────
   CALL FRAME — إطار الاستدعاء
   ───────────────────────────────────────── */

typedef struct {
    Chunk   *chunk;
    uint32_t ip;
    Value   *vars[VARS_MAX];
} CallFrame;

static CallFrame g_frames[FRAMES_MAX];
static int       g_frame_depth = 0;

/* ─────────────────────────────────────────
   ATTRIBUTE ACCESS — الوصول للخصائص
   ───────────────────────────────────────── */

static Value *attr_get(Value *obj, const char *name) {
    if (obj->type == VAL_STR) {
        if (strcmp(name, "len") == 0)
            return make_int((int64_t)strlen(obj->as.s));
        if (strcmp(name, "upper") == 0) {
            char *s = strdup(obj->as.s);
            for (int i = 0; s[i]; i++)
                if (s[i] >= 'a' && s[i] <= 'z') s[i] -= 32;
            Value *r = make_str(s); free(s); return r;
        }
        if (strcmp(name, "lower") == 0) {
            char *s = strdup(obj->as.s);
            for (int i = 0; s[i]; i++)
                if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 32;
            Value *r = make_str(s); free(s); return r;
        }
    }
    if (obj->type == VAL_LIST) {
        List *lst = obj->as.list;
        if (strcmp(name, "len") == 0)
            return make_int((int64_t)lst->count);
    }
    if (obj->type == VAL_DICT) {
        Dict *d = obj->as.dict;
        if (strcmp(name, "len") == 0)
            return make_int((int64_t)d->count);
    }
    fprintf(stderr, "[SM] \"%s\" غير موجود\n", name);
    exit(1);
}

/* ─────────────────────────────────────────
   EXECUTE CHUNK — تنفيذ مقطع كود
   ───────────────────────────────────────── */

static Value *exec_chunk(Chunk *chunk, Value **args, int argc) {
    /* إعداد الإطار */
    if (g_frame_depth >= FRAMES_MAX) {
        fprintf(stderr, "[SM] فيضان إطارات الاستدعاء\n"); exit(1);
    }
    CallFrame *frame = &g_frames[g_frame_depth++];
    frame->chunk = chunk;
    frame->ip    = 0;
    memset(frame->vars, 0, sizeof(frame->vars));

    /* تمرير الوسائط — args تُخزَّن بترتيب أسماء المتغيرات */
    for (int i = 0; i < argc && i < chunk->name_count; i++)
        frame->vars[i] = args[i];

    uint8_t *code = chunk->code;
    uint32_t ip   = 0;

#define READ_U32() ({ \
    uint32_t _v = ((uint32_t)code[ip]<<24)|((uint32_t)code[ip+1]<<16)| \
                  ((uint32_t)code[ip+2]<<8)|(uint32_t)code[ip+3]; \
    ip+=4; _v; })

#define PUSH(v) vm_push(v)
#define POP()   vm_pop()

    while (ip < (uint32_t)chunk->code_len) {
        uint8_t op = code[ip++];

        switch (op) {

        /* ── Stack Ops ── */
        case OP_PUSH_INT: {
            uint32_t n = READ_U32();
            PUSH(make_int((int64_t)n));
            break;
        }
        case OP_PUSH_FLOAT: {
            /* 8 bytes big-endian IEEE 754 */
            uint8_t buf[8];
            for (int i = 0; i < 8; i++) buf[7-i] = code[ip+i];
            ip += 8;
            double d; memcpy(&d, buf, 8);
            PUSH(make_float(d));
            break;
        }
        case OP_PUSH_STR: {
            uint32_t idx = READ_U32();
            PUSH(make_str(chunk->consts[idx]->as.s));
            break;
        }
        case OP_PUSH_BOOL: {
            uint32_t b = READ_U32();
            PUSH(make_bool(b));
            break;
        }
        case OP_PUSH_NULL:
            PUSH(make_null());
            break;
        case OP_POP:
            POP();
            break;
        case OP_DUP:
            PUSH(vm_peek());
            break;

        /* ── Variables ── */
        case OP_LOAD: {
            uint32_t idx  = READ_U32();
            Value   *val  = frame->vars[idx];
            if (!val) {
                /* التحقق من المتغيرات العالمية */
                val = g_frames[0].vars[idx];
            }
            if (!val) {
                fprintf(stderr, "[SM] متغير غير معرّف: %s\n",
                        idx < (uint32_t)chunk->name_count ? chunk->names[idx] : "?");
                exit(1);
            }
            PUSH(val);
            break;
        }
        case OP_STORE: {
            uint32_t idx   = READ_U32();
            frame->vars[idx] = POP();
            break;
        }
        case OP_LOAD_CONST: {
            uint32_t idx = READ_U32();
            PUSH(chunk->consts[idx]);
            break;
        }

        /* ── Arithmetic ── */
        case OP_ADD: {
            Value *b = POP(), *a = POP();
            if (a->type == VAL_STR && b->type == VAL_STR) {
                size_t la = strlen(a->as.s), lb = strlen(b->as.s);
                char  *s  = (char*)malloc(la + lb + 1);
                strcpy(s, a->as.s); strcat(s, b->as.s);
                Value *r = make_str(s); free(s); PUSH(r);
            } else if (a->type == VAL_INT && b->type == VAL_INT) {
                PUSH(make_int(a->as.i + b->as.i));
            } else {
                PUSH(make_float(val_to_dbl(a) + val_to_dbl(b)));
            }
            break;
        }
        case OP_SUB: {
            Value *b = POP(), *a = POP();
            if (a->type == VAL_INT && b->type == VAL_INT)
                PUSH(make_int(a->as.i - b->as.i));
            else
                PUSH(make_float(val_to_dbl(a) - val_to_dbl(b)));
            break;
        }
        case OP_MUL: {
            Value *b = POP(), *a = POP();
            if (a->type == VAL_INT && b->type == VAL_INT)
                PUSH(make_int(a->as.i * b->as.i));
            else
                PUSH(make_float(val_to_dbl(a) * val_to_dbl(b)));
            break;
        }
        case OP_DIV: {
            Value *b = POP(), *a = POP();
            double db = val_to_dbl(b);
            if (db == 0.0) { fprintf(stderr,"[SM] قسمة على صفر\n"); exit(1); }
            PUSH(make_float(val_to_dbl(a) / db));
            break;
        }
        case OP_MOD: {
            Value *b = POP(), *a = POP();
            if (a->type == VAL_INT && b->type == VAL_INT)
                PUSH(make_int(a->as.i % b->as.i));
            else
                PUSH(make_float(fmod(val_to_dbl(a), val_to_dbl(b))));
            break;
        }
        case OP_NEG: {
            Value *a = POP();
            if (a->type == VAL_INT)   PUSH(make_int(-a->as.i));
            else                       PUSH(make_float(-val_to_dbl(a)));
            break;
        }

        /* ── Compare ── */
        case OP_EQ:  { Value *b=POP(),*a=POP(); PUSH(make_bool(val_eq(a,b)));  break; }
        case OP_NEQ: { Value *b=POP(),*a=POP(); PUSH(make_bool(!val_eq(a,b))); break; }
        case OP_LT:  { Value *b=POP(),*a=POP(); PUSH(make_bool(val_to_dbl(a)< val_to_dbl(b))); break; }
        case OP_GT:  { Value *b=POP(),*a=POP(); PUSH(make_bool(val_to_dbl(a)> val_to_dbl(b))); break; }
        case OP_LE:  { Value *b=POP(),*a=POP(); PUSH(make_bool(val_to_dbl(a)<=val_to_dbl(b))); break; }
        case OP_GE:  { Value *b=POP(),*a=POP(); PUSH(make_bool(val_to_dbl(a)>=val_to_dbl(b))); break; }

        /* ── Logic ── */
        case OP_AND: { Value *b=POP(),*a=POP(); PUSH(make_bool(val_truthy(a)&&val_truthy(b))); break; }
        case OP_OR:  { Value *b=POP(),*a=POP(); PUSH(make_bool(val_truthy(a)||val_truthy(b))); break; }
        case OP_NOT: { Value *a=POP(); PUSH(make_bool(!val_truthy(a))); break; }

        /* ── Jumps ── */
        case OP_JMP: {
            uint32_t target = READ_U32();
            ip = target;
            break;
        }
        case OP_JMP_IF: {
            uint32_t target = READ_U32();
            if (val_truthy(POP())) ip = target;
            break;
        }
        case OP_JMP_IFNOT: {
            uint32_t target = READ_U32();
            if (!val_truthy(POP())) ip = target;
            break;
        }

        /* ── Functions ── */
        case OP_MAKE_FN: {
            uint32_t idx  = READ_U32();
            const char *fname = chunk->consts[idx]->as.s;
            Chunk *fn_chunk   = find_chunk(fname);
            if (!fn_chunk) {
                fprintf(stderr, "[SM] دالة غير موجودة: %s\n", fname);
                exit(1);
            }
            FnVal *fnv  = (FnVal*)calloc(1, sizeof(FnVal));
            fnv->chunk  = fn_chunk;
            fnv->refs   = 1;
            Value *fval = alloc_val(VAL_FN);
            fval->as.fn = fnv;
            PUSH(fval);
            break;
        }
        case OP_CALL: {
            uint32_t argc_n = READ_U32();
            Value   *fn_val = POP();
            Value  **call_args = (Value**)malloc(argc_n * sizeof(Value*));
            /* args pushed left-to-right, pop in reverse */
            for (int i = (int)argc_n - 1; i >= 0; i--)
                call_args[i] = POP();
            if (!fn_val || fn_val->type != VAL_FN) {
                fprintf(stderr, "[SM] لا يمكن استدعاء قيمة من نوع غير دالة\n");
                exit(1);
            }
            Value *result = exec_chunk(fn_val->as.fn->chunk, call_args, (int)argc_n);
            free(call_args);
            PUSH(result ? result : make_null());
            break;
        }
        case OP_RETURN: {
            Value *ret = POP();
            g_frame_depth--;
            return ret;
        }

        /* ── Collections ── */
        case OP_MAKE_LIST: {
            uint32_t count = READ_U32();
            Value   *lst   = make_list();
            /* items are on stack: first item pushed first */
            Value  **tmp = (Value**)malloc(count * sizeof(Value*));
            for (int i = (int)count - 1; i >= 0; i--)
                tmp[i] = POP();
            for (uint32_t i = 0; i < count; i++)
                list_push(lst->as.list, tmp[i]);
            free(tmp);
            PUSH(lst);
            break;
        }
        case OP_MAKE_DICT: {
            uint32_t count = READ_U32();
            Value   *dict  = make_dict();
            Value  **tmp   = (Value**)malloc(count * 2 * sizeof(Value*));
            for (int i = (int)(count*2) - 1; i >= 0; i--)
                tmp[i] = POP();
            for (uint32_t i = 0; i < count; i++)
                dict_set(dict->as.dict, tmp[i*2], tmp[i*2+1]);
            free(tmp);
            PUSH(dict);
            break;
        }
        case OP_INDEX_GET: {
            Value *idx = POP(), *obj = POP();
            if (obj->type == VAL_LIST) {
                int64_t i = idx->as.i;
                List   *l = obj->as.list;
                if (i < 0) i += l->count;
                if (i < 0 || i >= l->count) {
                    fprintf(stderr, "[SM] فهرس خارج النطاق: %lld\n", (long long)i);
                    exit(1);
                }
                PUSH(l->items[i]);
            } else if (obj->type == VAL_DICT) {
                Value *val = dict_get(obj->as.dict, idx);
                PUSH(val ? val : make_null());
            } else if (obj->type == VAL_STR) {
                int64_t i = idx->as.i;
                int     n = (int)strlen(obj->as.s);
                if (i < 0) i += n;
                if (i < 0 || i >= n) {
                    fprintf(stderr, "[SM] فهرس خارج النطاق\n"); exit(1);
                }
                char buf[2] = { obj->as.s[i], '\0' };
                PUSH(make_str(buf));
            } else {
                fprintf(stderr, "[SM] لا يمكن الفهرسة\n"); exit(1);
            }
            break;
        }
        case OP_INDEX_SET: {
            READ_U32(); /* unused */
            Value *val = POP(), *idx = POP(), *obj = POP();
            if (obj->type == VAL_LIST) {
                int64_t i = idx->as.i;
                List   *l = obj->as.list;
                if (i < 0) i += l->count;
                if (i < 0 || i >= l->count) {
                    fprintf(stderr, "[SM] فهرس خارج النطاق\n"); exit(1);
                }
                l->items[i] = val;
            } else if (obj->type == VAL_DICT) {
                dict_set(obj->as.dict, idx, val);
            }
            PUSH(obj);
            break;
        }
        case OP_ATTR_GET: {
            uint32_t idx = READ_U32();
            Value   *obj = POP();
            const char *name = chunk->consts[idx]->as.s;
            PUSH(attr_get(obj, name));
            break;
        }
        case OP_ATTR_SET: {
            uint32_t idx = READ_U32();
            Value *val = POP(), *obj = POP();
            if (obj->type == VAL_DICT) {
                dict_set(obj->as.dict,
                         make_str(chunk->consts[idx]->as.s), val);
            }
            break;
        }

        /* ── IO ── */
        case OP_PRINT: {
            uint32_t count = READ_U32();
            Value  **tmp   = (Value**)malloc(count * sizeof(Value*));
            for (int i = (int)count - 1; i >= 0; i--)
                tmp[i] = POP();
            for (uint32_t i = 0; i < count; i++) {
                if (i > 0) printf(" ");
                print_val(tmp[i]);
            }
            printf("\n");
            free(tmp);
            break;
        }
        case OP_INPUT: {
            READ_U32();
            Value *prompt = POP();
            printf("%s", prompt->type == VAL_STR ? prompt->as.s : "");
            fflush(stdout);
            char buf[1024];
            if (!fgets(buf, sizeof(buf), stdin))
                buf[0] = '\0';
            /* إزالة newline */
            int len = (int)strlen(buf);
            if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
            PUSH(make_str(buf));
            break;
        }

        case OP_HALT:
            goto done;

        default:
            fprintf(stderr, "[SM] opcode غير معروف: 0x%02X عند %u\n", op, ip-1);
            exit(1);
        }
    }

done:
    g_frame_depth--;
    return make_null();

#undef READ_U32
#undef PUSH
#undef POP
}

/* ─────────────────────────────────────────
   MAIN — نقطة الدخول
   ───────────────────────────────────────── */

static void load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[SM] لا يمكن فتح الملف: %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    g_file_size = (size_t)ftell(f);
    rewind(f);
    g_file_data = (uint8_t*)malloc(g_file_size);
    fread(g_file_data, 1, g_file_size, f);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "Shield VM v1.0\n"
            "الاستخدام: shield-vm file.smbc\n"
            "البناء:    clang shield_vm.c -o shield-vm -lm\n");
        return 1;
    }

    load_file(argv[1]);

    /* التحقق من Magic header */
    if (g_file_size < 4 ||
        memcmp(g_file_data, "SMbc", 4) != 0) {
        fprintf(stderr, "[SM] ليس ملف Shield bytecode صالح (.smbc)\n");
        return 1;
    }

    size_t pos = 4;

    /* Version */
    uint32_t version = read_u32(&pos);
    (void)version;

    /* تحميل جميع الـ chunks */
    uint32_t chunk_count = read_u32(&pos);
    g_chunk_count = 0;

    for (uint32_t i = 0; i < chunk_count; i++) {
        uint32_t chunk_size = read_u32(&pos);
        size_t   start_pos  = pos;
        Chunk   *c          = load_chunk(&pos);
        g_chunks[g_chunk_count++] = c;
        /* تأكد من قراءة الحجم الصحيح */
        pos = start_pos + chunk_size;
    }

    if (g_chunk_count == 0) {
        fprintf(stderr, "[SM] لا توجد chunks في الملف\n");
        return 1;
    }

    /* إعداد الآلة الافتراضية */
    g_vm = (VMState*)calloc(1, sizeof(VMState));
    g_frame_depth = 0;

    /* تشغيل البرنامج الرئيسي (أول chunk) */
    exec_chunk(g_chunks[0], NULL, 0);

    /* تنظيف */
    free(g_vm);
    free(g_file_data);

    return 0;
}
