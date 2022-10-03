/*
 * Smatch pattern to facilitate the hardening of the Linux guest kernel
 * for Confidential Cloud Computing threat model. 
 * In this model the Linux guest kernel cannot trust the values
 * it obtains using low level IO functions because they can be provided
 * by a potentially malicious host or VMM. Instead it needs to make
 * sure the code that handles processing of such values is hardened,
 * free of memory safety issues and other potential security issues. 
 *
 * This smatch pattern helps to indentify such places.
 * Currently it covers most of MSR, portIO, MMIO, PCI config space
 * and cpuid reading primitives.
 * The full list of covered functions is stored in host_input_funcs array.
 * The output of the pattern can be used to facilitate code audit, as
 * well as to verify that utilized fuzzing strategy can reach all the
 * code paths that can take a low-level input from a potentially malicious host.
 *
 * When ran, the pattern produces two types of findings: errors and warnings.
 * This is done to help prioritizing the issues for the manual code audit.
 * However, if time permits, all locations reported by the pattern should be checked. 
 *
 * Written based on existing smatch patterns.
 * 
 * Author: Elena Reshetova <elena.reshetova@intel.com>
 * Copyright (c) 2022, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"
#include <math.h>

STATE(called_funcs);
static int my_id;
static const char* pattern_name = "check_host_input";

/* Obtain the line number where a current function
 * starts. Used to calculate a relative offset for
 * the pattern findings. */
static int get_func_start_lineno(char* func_name)
{
    struct sm_state *sm;
    
    if (!func_name)
        return -1;

    FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
        if ( (sm->sym) && (strstr(func_name, sm->name) != NULL) 
        && (slist_has_state(sm->possible, &called_funcs)))
            return sm->sym->pos.line;
    } END_FOR_EACH_SM(sm);
    return -1;
}

/* Calculate djb2 hash */
unsigned long djb2_hash(const char *str, int num)
{
        unsigned long hash = 5381;
        int c;

        while ((c = *str++))
            hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
        return ((hash << 5) + hash) + num;
}

/* Produce the djb2 hash from a given expression.
 * Used in order to generate unique identifies for each
 * reported issue. These identifiers are used then
 * to automatically transfer previously seen results. */
unsigned long produce_expression_hash(struct expression *expr)
{
    unsigned long hash = 0;
    int line_offset = get_lineno() - get_func_start_lineno(get_function());
    const char *str = expr_to_str(expr);

    /* for non-parsable exressions and expressions
     * contatining temp variables (like __UNIQUE_ID_*, $expr_), it is
     * more stable to use a fix string for hasing together
     * with line offset to avoid many results that do not
     * automatically transfer between the audits on different
     * versions */

    if (str && !(strstr(str, "__UNIQUE_ID_")) && !(strstr(str, "$expr_")))
        hash = djb2_hash(str, line_offset);
    else
        hash = djb2_hash("complex", line_offset);
    return hash;
}

/* Helper utility to remove various operands
 * to get a clean expression */
static struct expression* strip_pre_post_ops(struct expression *expr)
{
    while (expr) {
        if((expr->type == EXPR_PREOP) || (expr->type == EXPR_POSTOP)) {
            expr = expr->unop;
        } else if ((expr->type == EXPR_CAST) || (expr->type == EXPR_FORCE_CAST)
            || (expr->type == EXPR_IMPLIED_CAST)) {
            expr = expr->cast_expression;
        } else {
            // Done if we can't strip anything more
            break;
        }
    }
    return expr;
}

/* Helper to store the info on called functions.
 * Used to calculate the line number in get_func_start_lineno() */
static void match_function_def(struct symbol *sym)
{
    set_state(my_id, sym->ident->name, sym, &called_funcs);
}

/* Checks all return expressions for tainted values */
static void match_return(struct expression *ret_value)
{
    unsigned long hash;

    if (!ret_value)
        return;

    if (is_host_rl(ret_value)) {
        hash = produce_expression_hash(ret_value);
        sm_warning("{%lu}\n\t'%s' return an expression containing a propagated value from the host '%s';",
                    hash, pattern_name, expr_to_str(ret_value));
    }
}


/* Checks all STMT_ITERATOR/IF/SWITCH expressions for tainted values */
static void match_statement(struct statement *stmt)
{
    unsigned long hash;
    struct expression *expr = NULL;

    if (!stmt)
        return;

    if (stmt->type == STMT_ITERATOR) {
        if ((stmt->iterator_pre_statement) && (stmt->iterator_pre_statement->type == STMT_EXPRESSION) 
            && (stmt->iterator_pre_statement->expression) 
            && (is_host_rl(stmt->iterator_pre_statement->expression)))
            expr = stmt->iterator_pre_statement->expression;

        if ((stmt->iterator_post_statement) && (stmt->iterator_post_statement->type == STMT_EXPRESSION)
            && (stmt->iterator_post_statement->expression)
            && (is_host_rl(stmt->iterator_post_statement->expression)))
            expr = stmt->iterator_post_statement->expression;

        if ((stmt->iterator_pre_condition) && (is_host_rl(stmt->iterator_pre_condition)))
            expr = stmt->iterator_pre_condition;

        if ((stmt->iterator_post_condition) && (is_host_rl(stmt->iterator_post_condition)))
            expr = stmt->iterator_post_condition;

        /* The above logic only stores the latest tainted expr.
         * This is ok since one warning per line is enough */
        if (expr) {
            hash = produce_expression_hash(expr);
            sm_error("{%lu}\n\t'%s' an expression containing a propagated value from the host '%s' used in iterator;",
                    hash, pattern_name, expr_to_str(expr));
            return;
        }
    } else if (stmt->type == STMT_IF) {
        expr = stmt->if_conditional;
    } else if (stmt->type == STMT_SWITCH) {
        expr = stmt->switch_expression;
    } else if (stmt->type == STMT_RETURN){
        return; /* returns are handled by match_return */
    }

    if (!expr)
        return;

    hash = produce_expression_hash(expr);
    if (is_host_rl(expr)){
        sm_warning("{%lu}\n\t'%s' an expression containing a propagated value from the host '%s' used in if/switch statement;",
                hash, pattern_name, expr_to_str(expr));
        return;
    }
}

/* Helper to rule out the temp expressions */
bool is_tmp_expression(struct expression *expr)
{
    if (expr_to_str(expr))
        if ((strncmp(expr_to_str(expr), "__fake_", 7) == 0) ||
            (strncmp(expr_to_str(expr), "__UNIQUE_ID", 11) == 0) ||
            (strncmp(expr_to_str(expr), "$expr_", 6) == 0))
            return true;
    return false;
}

/* Checks assigment expressions */
static void match_assign(struct expression *expr)
{
    struct expression *current = expr;
    struct expression *left = NULL;
    unsigned long hash = 0;

    if (!current)
        return;

    if (is_fake_var_assign(current))
        return;

    if (__in_fake_parameter_assign)
        return;

    if (current->type != EXPR_ASSIGNMENT) {
        sm_error("'%s' Strange EXPR in assigment;", pattern_name);
        return;
    }

    hash = produce_expression_hash(expr);
    left = current->left;
    left = strip_pre_post_ops(left);
    current = strip_expr(current->right);

    if (is_tmp_expression(current) || is_tmp_expression(left))
        return;

    if (current->type == EXPR_CALL) {
        int param = get_host_data_fn_param(expr_to_str(current->fn));
        if (param == -1) {
            sm_warning("{%lu}\n\t'%s' read from the host using function '%s' into a variable '%s';",
                hash, pattern_name, expr_to_str(current->fn), expr_to_str(left));
        } 
        /* rest of the cases are handled in match_after_call */
        return;
    }

    if (!is_host_rl(current))
        return;

    sm_warning("{%lu}\n\t'%s' propagating read value from the host '%s' into a different variable '%s';",
        hash, pattern_name, expr_to_str(current), expr_to_str(left));
    return;
}

/* Checks function calls */
static void match_after_call(struct expression *expr)
{
    struct expression *arg;
    unsigned long hash;
    const char *message, *function_name;
    int param = get_host_data_fn_param(expr_to_str(expr->fn));

    if ((!expr) || (!expr->fn))
        return;

    if (parse_error)
        return;

    if (is_impossible_path())
        return;

    if (!expr->fn->symbol_name)
        function_name = expr_to_str(expr);
    else
        function_name = expr->fn->symbol_name->name;

    hash = produce_expression_hash(expr);

    FOR_EACH_PTR(expr->args, arg) {
        if (!is_host_rl(arg) && !points_to_host_data(arg))
            continue;

        /* the case when param = -1 is handled in match_assign */
        if (param > 0)
            sm_warning("{%lu}\n\t'%s' read from the host using function '%s' into a non-local variable '%s';",
                hash, pattern_name, expr_to_str(expr->fn), expr_to_str(arg));
        else {
            if (arg->type == EXPR_BINOP) 
                message = "{%lu}\n\t'%s' an expression containing a tainted value from the host '%s' used in function '%s';";
            else
                message = "{%lu}\n\t'%s' a tainted value from the host '%s' used in function '%s';";
            sm_warning(message, hash, pattern_name, expr_to_str(arg), function_name);
        }
    } END_FOR_EACH_PTR(arg);
}

/* Checks if the array offset has
 * been influenced by a value supplied by host */
static void array_offset_check(struct expression *expr)
{
    struct expression *offset;

    expr = strip_expr(expr);
    if (!is_array(expr))
        return;

    if (is_impossible_path())
        return;

    offset = get_array_offset(expr);
    if (!is_host_rl(offset))
        return;

    sm_error("'%s' a tainted value from the host '%s' used as array offset in expression '%s';",
            pattern_name, expr_to_str(offset), expr_to_str(expr));
    return;
}

void check_host_input(int id)
{
    my_id = id;
    add_hook(&match_assign, ASSIGNMENT_HOOK);
    add_hook(&match_return, RETURN_HOOK);
    add_hook(&match_statement, STMT_HOOK);
    add_hook(&match_function_def, AFTER_DEF_HOOK);
    add_hook(&match_after_call, FUNCTION_CALL_HOOK_AFTER_DB);
    add_hook(&array_offset_check, OP_HOOK);
}
