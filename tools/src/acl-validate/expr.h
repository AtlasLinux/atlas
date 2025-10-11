#ifndef EXPR_H
#define EXPR_H

// Evaluate a C-style expression (with casts, unary/binary/ternary, string concatenation).
// Returns a malloc'd C-string with the result (numeric or string). NULL on parse/eval error.
// Caller must free() the returned string.
char *expr_eval_to_string(const char *expr_text);

#endif // EXPR_H
