from collections import defaultdict
import sys
import pprint

from constants import (
    BLACKLIST,
    IMPL_DEP_FILE_STR,
    OUTPUT_FILE_STR,
    SYSCALL_PREFIXES,
    ListType,
    hardcode_syscall_read_fields,
    hardcode_syscall_write_fields,
)

class Parser(object):
    def __init__(
        self,
        impl_dep_file_str=IMPL_DEP_FILE_STR,
        output_file_str=OUTPUT_FILE_STR,
        verbose=False
    ):
        try:
            self.impl_dep_file = file(impl_dep_file_str, 'r')
            self.output_file = file(output_file_str, 'w+')
            if verbose:
                self.output_file_verbose = file(output_file_str + '_verbose', 'w+')
        except IOError:
            sys.stderr.write("ERROR: Cannot open files %s %s.\n" % (impl_dep_file_str, output_file_str))
            sys.exit(1)
        self.verbose = verbose
        self.syscall_read_fields = defaultdict(set)
        self.syscall_write_fields = defaultdict(set)
        self.implicit_dependencies = defaultdict(set)
        self.verbose_impl_dep = defaultdict(list)

        for syscall,fields in hardcode_syscall_read_fields.iteritems():
            self.syscall_read_fields[syscall].update(set(fields))

        for syscall,fields in hardcode_syscall_write_fields.iteritems():
            self.syscall_write_fields[syscall].update(set(fields))

    def _sanitize_syscall(self, syscall):
        for prefix in SYSCALL_PREFIXES:
            if syscall.startswith(prefix):
                return syscall[len(prefix):]
        return syscall

    def _deref_to_tuple(self, deref):
        """ (struct a)->b ==> (a,b) """
        struct, member = deref.split('->')
        struct = struct[1:-1]  # strip parens
        struct = struct.split(' ')[1]  # drop struct keyword
        return (struct, member)

    def _split_field(self, field):
        field = field.strip()
        field = field[1: -1]  # strip square brackets
        derefs = [struct.strip() for struct in field.strip().split(',') if struct]
        return map(
            lambda deref: self._deref_to_tuple(deref),
            derefs
        )

    def _sanitize_line(self, line):
        syscall_and_listtype, field = line.split(':')
        syscall, list_type = syscall_and_listtype.split(' ')
        syscall = self._sanitize_syscall(syscall)
        derefs = self._split_field(field)
        return syscall, list_type, derefs

    def _add_fields(self, syscall, list_type, derefs):
        if list_type == ListType.READ:
            d = self.syscall_read_fields
        elif list_type == ListType.WRITE:
            d = self.syscall_write_fields
        for deref in derefs:
            if deref in BLACKLIST:  # ignore spammy structs
                continue
            d[syscall].add(deref)

    def _construct_implicit_deps(self):
        """ just do a naive O(n^2) loop to see intersections between write_list and read_list """
        for this_call,read_fields in self.syscall_read_fields.iteritems():
            for that_call,write_fields in self.syscall_write_fields.iteritems():
                intersection = read_fields & write_fields
                if intersection:
                    self.implicit_dependencies[this_call].add(that_call)
                if intersection and self.verbose:
                    self.verbose_impl_dep[this_call].append({
                        'call': that_call,
                        'reason': intersection,
                    })

    def parse(self):
        for line in self.impl_dep_file:
            syscall, list_type, derefs = self._sanitize_line(line)
            self._add_fields(syscall, list_type, derefs)
        # pprint.pprint(dict(self.syscall_write_fields))
        # pprint.pprint(dict(self.syscall_read_fields))
        self._construct_implicit_deps()
        # pprint.pprint(dict(self.implicit_dependencies))
        # pprint.pprint(dict(self.verbose_impl_dep))

    def write(self):
        pprint.pprint(dict(self.implicit_dependencies), self.output_file)
        if self.verbose:
            pprint.pprint(dict(self.verbose_impl_dep), self.output_file_verbose)

    def close(self):
        self.output_file.close()
        self.impl_dep_file.close()
        if self.verbose:
            self.output_file_verbose.close()
