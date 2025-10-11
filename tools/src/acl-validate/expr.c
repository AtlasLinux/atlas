// expr_eval.c
// Parse C‐style expressions with casts and then evaluate to a single string.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef enum {
    T_END, T_INT, T_DOUBLE, T_STRING, T_IDENT,
    T_OP, T_QUESTION, T_COLON, T_LPAREN, T_RPAREN
} TokenType;

typedef struct {
    TokenType type;
    char *text;
} Token;

const char *src;
Token curtok;

// AST node kinds
typedef enum {
    NODE_INT, NODE_DOUBLE, NODE_STRING, NODE_IDENT,
    NODE_CAST, NODE_UNARY, NODE_BINARY, NODE_TERNARY
} NodeType;

typedef enum {
    OP_NEG, OP_NOT, OP_MUL, OP_DIV, OP_MOD,
    OP_ADD, OP_SUB, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_EQ, OP_NE, OP_AND, OP_OR
} OpKind;

typedef struct Node {
    NodeType type;
    OpKind op;
    char   *text;      // for strings, id, cast type
    long    ival;
    double  dval;
    struct Node *a, *b, *c;
} Node;

// Evaluated value
typedef enum { V_INT, V_DOUBLE, V_STRING } ValType;
typedef struct {
    ValType type;
    long    ival;
    double  dval;
    char   *sval;
} Value;

//----------------------------------------------------------------
// Lexer
//----------------------------------------------------------------
void lex_error(const char *msg) {
    fprintf(stderr, "Lex error: %s at '%s'\n", msg, src);
    exit(1);
}

void next_tok() {
    while (isspace(*src)) src++;
    if (*src == '\0') {
        curtok.type = T_END;
        curtok.text = NULL;
        return;
    }
    if (*src == '"') {
        const char *start = ++src;
        while (*src && *src != '"') {
            if (*src == '\\' && src[1]) src++;
            src++;
        }
        if (*src != '"') lex_error("unterminated string");
        size_t len = src - start;
        char *buf = malloc(len+1);
        strncpy(buf, start, len);
        buf[len] = '\0';
        src++;
        curtok.type = T_STRING;
        curtok.text = buf;
        return;
    }
    if (isdigit(*src) || (*src == '.' && isdigit(src[1]))) {
        const char *start = src;
        while (isdigit(*src)) src++;
        int is_double = 0;
        if (*src == '.') {
            is_double = 1;
            src++;
            while (isdigit(*src)) src++;
        }
        size_t len = src - start;
        char *buf = malloc(len+1);
        strncpy(buf, start, len);
        buf[len] = '\0';
        curtok.type = is_double ? T_DOUBLE : T_INT;
        curtok.text = buf;
        return;
    }
    if (isalpha(*src) || *src == '_' || *src == '$') {
        const char *start = src;
        while (isalnum(*src) || *src=='_' || *src=='$' || *src=='.') src++;
        size_t len = src - start;
        char *buf = malloc(len+1);
        strncpy(buf, start, len);
        buf[len] = '\0';
        curtok.type = T_IDENT;
        curtok.text = buf;
        return;
    }
    // two-char ops
    if ((src[0]=='<'&&src[1]=='=')||(src[0]=='>'&&src[1]=='=')||
        (src[0]=='='&&src[1]=='=')||(src[0]=='!'&&src[1]=='=')||
        (src[0]=='&'&&src[1]=='&')||(src[0]=='|'&&src[1]=='|')) {
        char buf[3] = {src[0], src[1], 0};
        curtok.type = T_OP;
        curtok.text = strdup(buf);
        src += 2;
        return;
    }
    // single-char
    char c = *src++;
    switch (c) {
      case '+': case '-': case '*': case '/': case '%':
      case '<': case '>': case '!':
        curtok.type = T_OP;
        curtok.text = malloc(2);
        curtok.text[0] = c;
        curtok.text[1] = '\0';
        return;
      case '?':
        curtok.type = T_QUESTION; curtok.text = strdup("?"); return;
      case ':':
        curtok.type = T_COLON; curtok.text = strdup(":"); return;
      case '(':
        curtok.type = T_LPAREN; curtok.text = strdup("("); return;
      case ')':
        curtok.type = T_RPAREN; curtok.text = strdup(")"); return;
    }
    lex_error("unknown char");
}

//----------------------------------------------------------------
// Parser (prec‐climbing)
//----------------------------------------------------------------
#define MATCH(t) (curtok.type==t ? (next_tok(),1) : 0)

Node *parse_expr();
Node *parse_ternary();
Node *parse_logical_or();
Node *parse_logical_and();
Node *parse_equality();
Node *parse_comparison();
Node *parse_add();
Node *parse_mul();
Node *parse_unary();
Node *parse_primary();

Node *make_node(NodeType t) {
    Node *n = calloc(1, sizeof(*n));
    n->type = t;
    return n;
}
Node *make_int(long v) {
    Node *n = make_node(NODE_INT); n->ival = v; return n;
}
Node *make_double(double v) {
    Node *n = make_node(NODE_DOUBLE); n->dval = v; return n;
}
Node *make_string(char *s) {
    Node *n = make_node(NODE_STRING); n->text = s; return n;
}
Node *make_ident(char *s) {
    Node *n = make_node(NODE_IDENT); n->text = s; return n;
}

Node *parse_expr() {
    return parse_ternary();
}

Node *parse_ternary() {
    Node *c = parse_logical_or();
    if (MATCH(T_QUESTION)) {
        Node *t = parse_expr();
        if (!MATCH(T_COLON)) {
            fprintf(stderr,"expected ':' in ternary\n");
            exit(1);
        }
        Node *e = parse_expr();
        Node *n = make_node(NODE_TERNARY);
        n->a = c; n->b = t; n->c = e;
        return n;
    }
    return c;
}

Node *make_cmp(Node *l, Node *r, OpKind op) {
    Node *n = make_node(NODE_BINARY);
    n->op = op; n->a = l; n->b = r;
    return n;
}

Node *parse_logical_or() {
    Node *n = parse_logical_and();
    while (curtok.type==T_OP && strcmp(curtok.text,"||")==0) {
        next_tok();
        Node *r = parse_logical_and();
        Node *o = make_node(NODE_BINARY);
        o->op = OP_OR; o->a = n; o->b = r;
        n = o;
    }
    return n;
}

Node *parse_logical_and() {
    Node *n = parse_equality();
    while (curtok.type==T_OP && strcmp(curtok.text,"&&")==0) {
        next_tok();
        Node *r = parse_equality();
        Node *o = make_node(NODE_BINARY);
        o->op = OP_AND; o->a = n; o->b = r;
        n = o;
    }
    return n;
}

Node *parse_equality() {
    Node *n = parse_comparison();
    while (curtok.type==T_OP &&
           (strcmp(curtok.text,"==")==0||strcmp(curtok.text,"!=")==0)) {
        OpKind op = strcmp(curtok.text,"==")==0 ? OP_EQ : OP_NE;
        next_tok();
        Node *r = parse_comparison();
        n = make_cmp(n, r, op);
    }
    return n;
}

Node *parse_comparison() {
    Node *n = parse_add();
    while (curtok.type==T_OP &&
          (strcmp(curtok.text,"<")==0||strcmp(curtok.text,">")==0||
           strcmp(curtok.text,"<=")==0||strcmp(curtok.text,">=")==0)) {
        OpKind op;
        if (strcmp(curtok.text,"<")==0) op=OP_LT;
        else if (strcmp(curtok.text,">")==0) op=OP_GT;
        else if (strcmp(curtok.text,"<=")==0)op=OP_LE;
        else op=OP_GE;
        next_tok();
        Node *r = parse_add();
        n = make_cmp(n, r, op);
    }
    return n;
}

Node *parse_add() {
    Node *n = parse_mul();
    while (curtok.type==T_OP &&
          (strcmp(curtok.text,"+")==0||strcmp(curtok.text,"-")==0)) {
        OpKind op = strcmp(curtok.text,"+")==0 ? OP_ADD : OP_SUB;
        next_tok();
        Node *r = parse_mul();
        Node *o = make_node(NODE_BINARY);
        o->op = op; o->a = n; o->b = r;
        n = o;
    }
    return n;
}

Node *parse_mul() {
    Node *n = parse_unary();
    while (curtok.type==T_OP &&
          (strcmp(curtok.text,"*")==0||strcmp(curtok.text,"/")==0||
           strcmp(curtok.text,"%")==0)) {
        OpKind op;
        if (strcmp(curtok.text,"*")==0) op=OP_MUL;
        else if (strcmp(curtok.text,"/")==0) op=OP_DIV;
        else op=OP_MOD;
        next_tok();
        Node *r = parse_unary();
        Node *o = make_node(NODE_BINARY);
        o->op = op; o->a = n; o->b = r;
        n = o;
    }
    return n;
}

Node *parse_unary() {
    if (curtok.type==T_OP && strcmp(curtok.text,"-")==0) {
        next_tok();
        Node *c = parse_unary();
        Node *n = make_node(NODE_UNARY);
        n->op = OP_NEG; n->a = c;
        return n;
    }
    if (curtok.type==T_OP && strcmp(curtok.text,"!")==0) {
        next_tok();
        Node *c = parse_unary();
        Node *n = make_node(NODE_UNARY);
        n->op = OP_NOT; n->a = c;
        return n;
    }
    // cast
    if (curtok.type==T_LPAREN) {
        const char *bk_src = src;
        Token bk_tok = curtok;
        next_tok();
        if (curtok.type==T_IDENT) {
            char *ctype = strdup(curtok.text);
            next_tok();
            if (MATCH(T_RPAREN)) {
                Node *child = parse_unary();
                Node *n = make_node(NODE_CAST);
                n->text = ctype;
                n->a = child;
                return n;
            }
            free(ctype);
        }
        // rollback
        src = bk_src;
        curtok = bk_tok;
    }
    return parse_primary();
}

Node *parse_primary() {
    if (MATCH(T_LPAREN)) {
        Node *e = parse_expr();
        if (!MATCH(T_RPAREN)) {
            fprintf(stderr, "expected )\n");
            exit(1);
        }
        return e;
    }
    if (curtok.type==T_INT) {
        long v = strtol(curtok.text,NULL,10);
        Node *n = make_int(v);
        next_tok();
        return n;
    }
    if (curtok.type==T_DOUBLE) {
        double v = strtod(curtok.text,NULL);
        Node *n = make_double(v);
        next_tok();
        return n;
    }
    if (curtok.type==T_STRING) {
        Node *n = make_string(curtok.text);
        next_tok();
        return n;
    }
    if (curtok.type==T_IDENT) {
        Node *n = make_ident(curtok.text);
        next_tok();
        return n;
    }
    fprintf(stderr, "unexpected token '%s'\n",
            curtok.text?curtok.text:"(end)");
    exit(1);
}

//----------------------------------------------------------------
// Evaluator
//----------------------------------------------------------------
char *str_from_val(const Value *v) {
    char buf[64];
    if (v->type == V_INT) {
        snprintf(buf, sizeof(buf), "%ld", v->ival);
        return strdup(buf);
    }
    if (v->type == V_DOUBLE) {
        snprintf(buf, sizeof(buf), "%g", v->dval);
        return strdup(buf);
    }
    return strdup(v->sval);
}

Value eval(Node *n) {
    if (!n) exit(1);
    switch(n->type) {
      case NODE_INT:
        return (Value){V_INT, n->ival, 0, NULL};
      case NODE_DOUBLE:
        return (Value){V_DOUBLE, 0, n->dval, NULL};
      case NODE_STRING:
        return (Value){V_STRING, 0, 0, n->text};
      case NODE_IDENT:
        return (Value){V_STRING, 0, 0, n->text};
      case NODE_CAST: {
        Value v = eval(n->a);
        if (strcmp(n->text, "int")==0) {
            long i = (v.type==V_DOUBLE? (long)v.dval : v.ival);
            return (Value){V_INT, i,0,NULL};
        }
        if (strcmp(n->text,"double")==0) {
            double d = (v.type==V_INT? (double)v.ival : v.dval);
            return (Value){V_DOUBLE,0,d,NULL};
        }
        // fallback: no-op
        return v;
      }
      case NODE_UNARY: {
        Value v = eval(n->a);
        if (n->op==OP_NEG) {
            if (v.type==V_INT) return (Value){V_INT, -v.ival,0,NULL};
            if (v.type==V_DOUBLE) return (Value){V_DOUBLE,0,-v.dval,NULL};
        }
        if (n->op==OP_NOT) {
            int b = 0;
            if (v.type==V_INT) b = !v.ival;
            else if (v.type==V_DOUBLE) b = !(v.dval!=0.0);
            else b = (v.sval[0]=='\0');
            return (Value){V_INT, b,0,NULL};
        }
        return v;
      }
      case NODE_BINARY: {
        Value L = eval(n->a);
        Value R = eval(n->b);
        switch(n->op) {
          case OP_ADD:
            // if either is string → concat
            if (L.type==V_STRING || R.type==V_STRING) {
                char *s1 = (L.type==V_STRING? L.sval : str_from_val(&L));
                char *s2 = (R.type==V_STRING? R.sval : str_from_val(&R));
                char *res = malloc(strlen(s1)+strlen(s2)+1);
                strcpy(res, s1);
                strcat(res, s2);
                if (L.type!=V_STRING) free(s1);
                if (R.type!=V_STRING) free(s2);
                return (Value){V_STRING,0,0,res};
            }
            // numeric add
            if (L.type==V_DOUBLE || R.type==V_DOUBLE) {
                double d = (L.type==V_DOUBLE?L.dval:L.ival)
                         + (R.type==V_DOUBLE?R.dval:R.ival);
                return (Value){V_DOUBLE,0,d,NULL};
            }
            return (Value){V_INT, L.ival + R.ival,0,NULL};
          case OP_SUB:
            if (L.type==V_DOUBLE || R.type==V_DOUBLE) {
                double d = (L.type==V_DOUBLE?L.dval:L.ival)
                         - (R.type==V_DOUBLE?R.dval:R.ival);
                return (Value){V_DOUBLE,0,d,NULL};
            }
            return (Value){V_INT, L.ival - R.ival,0,NULL};
          case OP_MUL:
            if (L.type==V_DOUBLE || R.type==V_DOUBLE) {
                double d = (L.type==V_DOUBLE?L.dval:L.ival)
                         * (R.type==V_DOUBLE?R.dval:R.ival);
                return (Value){V_DOUBLE,0,d,NULL};
            }
            return (Value){V_INT, L.ival * R.ival,0,NULL};
          case OP_DIV:
            if (L.type==V_DOUBLE || R.type==V_DOUBLE) {
                double d = (L.type==V_DOUBLE?L.dval:L.ival)
                         / (R.type==V_DOUBLE?R.dval:R.ival);
                return (Value){V_DOUBLE,0,d,NULL};
            }
            return (Value){V_INT, L.ival / R.ival,0,NULL};
          case OP_MOD:
            return (Value){V_INT, L.ival % R.ival,0,NULL};
          case OP_LT: {
            int b = (L.type==V_DOUBLE||R.type==V_DOUBLE)
                    ? ((L.type==V_DOUBLE?L.dval:L.ival)
                       < (R.type==V_DOUBLE?R.dval:R.ival))
                    : (L.ival < R.ival);
            return (Value){V_INT, b,0,NULL};
          }
          case OP_GT: {
            int b = (L.type==V_DOUBLE||R.type==V_DOUBLE)
                    ? ((L.type==V_DOUBLE?L.dval:L.ival)
                       > (R.type==V_DOUBLE?R.dval:R.ival))
                    : (L.ival > R.ival);
            return (Value){V_INT, b,0,NULL};
          }
          case OP_LE: {
            int b = (L.type==V_DOUBLE||R.type==V_DOUBLE)
                    ? ((L.type==V_DOUBLE?L.dval:L.ival)
                       <= (R.type==V_DOUBLE?R.dval:R.ival))
                    : (L.ival <= R.ival);
            return (Value){V_INT, b,0,NULL};
          }
          case OP_GE: {
            int b = (L.type==V_DOUBLE||R.type==V_DOUBLE)
                    ? ((L.type==V_DOUBLE?L.dval:L.ival)
                       >= (R.type==V_DOUBLE?R.dval:R.ival))
                    : (L.ival >= R.ival);
            return (Value){V_INT, b,0,NULL};
          }
          case OP_EQ: {
            int b = 0;
            if (L.type==V_STRING||R.type==V_STRING) {
                char *s1 = str_from_val(&L);
                char *s2 = str_from_val(&R);
                b = strcmp(s1,s2)==0;
                free(s1); free(s2);
            } else if (L.type==V_DOUBLE||R.type==V_DOUBLE) {
                b = ((L.type==V_DOUBLE?L.dval:L.ival)
                     == (R.type==V_DOUBLE?R.dval:R.ival));
            } else {
                b = (L.ival == R.ival);
            }
            return (Value){V_INT, b,0,NULL};
          }
          case OP_NE: {
            int b = 0;
            if (L.type==V_STRING||R.type==V_STRING) {
                char *s1 = str_from_val(&L);
                char *s2 = str_from_val(&R);
                b = strcmp(s1,s2)!=0;
                free(s1); free(s2);
            } else if (L.type==V_DOUBLE||R.type==V_DOUBLE) {
                b = ((L.type==V_DOUBLE?L.dval:L.ival)
                     != (R.type==V_DOUBLE?R.dval:R.ival));
            } else {
                b = (L.ival != R.ival);
            }
            return (Value){V_INT, b,0,NULL};
          }
          case OP_AND: {
            int b = ((L.type==V_DOUBLE?L.dval:L.ival) &&
                     (R.type==V_DOUBLE?R.dval:R.ival));
            return (Value){V_INT, b,0,NULL};
          }
          case OP_OR: {
            int b = ((L.type==V_DOUBLE?L.dval:L.ival) ||
                     (R.type==V_DOUBLE?R.dval:R.ival));
            return (Value){V_INT, b,0,NULL};
          }
          default:
            break;
        }
      }
      case NODE_TERNARY: {
        Value cond = eval(n->a);
        int b = (cond.type==V_DOUBLE?cond.dval:cond.ival);
        return eval(b ? n->b : n->c);
      }
    }
    return (Value){V_INT,0,0,NULL};
}

//----------------------------------------------------------------
// Cleanup
//----------------------------------------------------------------
void free_ast(Node *n) {
    if (!n) return;
    free_ast(n->a);
    free_ast(n->b);
    free_ast(n->c);
    if (n->text) free(n->text);
    free(n);
}

char *expr_eval_to_string(const char *expr_text) {
    if (!expr_text) return NULL;

    // point your parser at the input string
    src = expr_text;
    next_tok();

    // parse
    Node *ast = parse_expr();
    if (curtok.type != T_END) {
        free_ast(ast);
        return NULL;
    }

    // evaluate
    Value v = eval(ast);
    free_ast(ast);

    // convert to string
    char *out = str_from_val(&v);
    return out;
}