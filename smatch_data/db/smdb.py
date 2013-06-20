#!/usr/bin/python

# Copyright (C) 2013 Oracle.
#
# Licensed under the Open Software License version 1.1

import sqlite3
import sys
import re

try:
    con = sqlite3.connect('smatch_db.sqlite')
except sqlite3.Error, e:
    print "Error %s:" % e.args[0]
    sys.exit(1)

def usage():
    print "%s <function> [table] [type] [parameter]" %(sys.argv[0])
    sys.exit(1)

function_ptrs = []
searched_ptrs = []
def get_function_pointers_helper(func):
    cur = con.cursor()
    cur.execute("select distinct ptr from function_ptr where function = '%s';" %(func))
    for row in cur:
        ptr = row[0]
        if ptr in function_ptrs:
            continue
        function_ptrs.append(ptr)
        if not ptr in searched_ptrs:
            searched_ptrs.append(ptr)
            get_function_pointers_helper(ptr)

def get_function_pointers(func):
    global function_ptrs
    global searched_ptrs
    function_ptrs = [func]
    searched_ptrs = [func]
    get_function_pointers_helper(func)
    return function_ptrs

db_types = [ "INTERNAL", "PARAM_VALUE", "BUF_SIZE", "USER_DATA", "CAPPED_DATA",
             "RETURN_VALUE", "DEREFERENCE", "RANGE_CAP", "LOCK_HELD",
             "LOCK_RELEASED", "ABSOLUTE_LIMITS", "LIMITED_VALUE",
             "ADDED_VALUE", "FILTER_VALUE", "PARAM_CLEARED",
             "UPPER_CONSTRAINT" ]

def type_to_str(type_int):

    t = int(type_int)
    if t < len(db_types):
        return db_types[t]
    return type_int

def type_to_int(type_string):
    for i in range(len(db_types)):
        if db_types[i] == type_string:
            return i
    return -1

def display_caller_info(printed, cur):
    for txt in cur:
        if not printed:
            print "file | caller | function | type | parameter | key | value |"
        printed = 1
        print "%20s | %20s | %20s |" %(txt[0], txt[1], txt[2]),
        print " %10s |" %(type_to_str(txt[5])),
        print " %d | %s | %s" %(txt[6], txt[7], txt[8])
    return printed

def get_caller_info(ptrs, my_type):
    cur = con.cursor()
    printed = 0
    type_filter = ""
    if my_type != "":
        type_filter = "and type = %d" %(type_to_int(my_type))
    for ptr in ptrs:
        cur.execute("select * from caller_info where function = '%s' %s;" %(ptr, type_filter))
        printed = display_caller_info(printed, cur)

def print_caller_info(func, my_type = ""):
    ptrs = get_function_pointers(func)
    get_caller_info(ptrs, my_type)

def print_return_states(func):
    cur = con.cursor()
    cur.execute("select * from return_states where function = '%s';" %(func))
    count = 0
    for txt in cur:
        printed = 1
        if count == 0:
            print "file | function | return_id | return_value | type | param | key | value |"
        count += 1
        print "%s | %s | %2s | %13s" %(txt[0], txt[1], txt[3], txt[4]),
        print "| %13s |" %(type_to_str(txt[6])),
        print " %2d | %20s | %20s |" %(txt[7], txt[8], txt[9])

def print_call_implies(func):
    cur = con.cursor()
    cur.execute("select * from call_implies where function = '%s';" %(func))
    count = 0
    for txt in cur:
        if not count:
            print "file | function | type | param | key | value |"
        count += 1
        print "%15s | %15s" %(txt[0], txt[1]),
        print "| %15s" %(type_to_str(txt[4])),
        print "| %3d | %15s |" %(txt[5], txt[6])

def print_type_size(var):
    cur = con.cursor()
    if not re.search("^\(struct ", var):
        m = re.search('(?<=->)\w+', var)
        print "searched"
        if not m:
            print "Can't determine type for %s" %(var)
            return
        var = "%->" + m.group(0)
    cur.execute("select * from type_size where type like '%s';" %(var))
    print "file | type | size"
    for txt in cur:
        printed = 1
        print "%15s | %15s | %d" %(txt[0], txt[1], txt[2])

def print_fn_ptrs(func):
    ptrs = get_function_pointers(func)
    if not ptrs:
        return
    print "%s = " %(func),
    i = 0
    for p in ptrs:
        if i > 0:
            print ",",
        i = i + 1
        print "'%s'" %(p),
    print ""

def get_callers(func):
    ret = []
    cur = con.cursor()
    ptrs = get_function_pointers(func)
    for ptr in ptrs:
        cur.execute("select distinct caller from caller_info where function = '%s';" %(ptr))
        for row in cur:
            ret.append(row[0])
    return ret

printed_funcs = []
def call_tree_helper(func, indent = 0):
    global printed_funcs
    if func in printed_funcs:
        return
    print "%s%s()" %(" " * indent, func)
    if func == "too common":
        return
    if indent > 6:
        return
    printed_funcs.append(func)
    callers = get_callers(func)
    if len(callers) >= 20:
        print "Over 20 callers for %s()" %(func)
        return
    for caller in callers:
        call_tree_helper(caller, indent + 2)

def print_call_tree(func):
    global printed_funcs
    printed_funcs = []
    call_tree_helper(func)

if len(sys.argv) < 2:
    usage()

if len(sys.argv) == 2:
    func = sys.argv[1]
    print_caller_info(func)
elif sys.argv[1] == "user_data":
    func = sys.argv[2]
    print_caller_info(func, "USER_DATA")
elif sys.argv[1] == "param_value":
    func = sys.argv[2]
    print_caller_info(func, "PARAM_VALUE")
elif sys.argv[1] == "function_ptr" or sys.argv[1] == "fn_ptr":
    func = sys.argv[2]
    print_fn_ptrs(func)
elif sys.argv[1] == "return_states":
    func = sys.argv[2]
    print_return_states(func)
elif sys.argv[1] == "call_implies":
    func = sys.argv[2]
    print_call_implies(func)
elif sys.argv[1] == "type_size":
    var = sys.argv[2]
    print_type_size(var)
elif sys.argv[1] == "call_tree":
    func = sys.argv[2]
    print_call_tree(func)
else:
    usage()
