#!/usr/bin/python

# glib-client-gen.py: "I Can't Believe It's Not dbus-binding-tool"
#
# Generate GLib client wrappers from the Telepathy specification.
# The master copy of this program is in the telepathy-glib repository -
# please make any changes there.
#
# Copyright (C) 2006-2008 Collabora Ltd. <http://www.collabora.co.uk/>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

import sys
import os.path
from getopt import gnu_getopt

from libglibcodegen import Signature, type_to_gtype, cmp_by_name, get_docstring

def camelcase_to_lower(s):
    out ="";
    out += s[0].lower()
    last_upper=False
    if s[0].isupper():
        last_upper=True
    for i in range(1,len(s)):
        if s[i].isupper():
            if last_upper:
                if (i+1) < len(s) and  s[i+1].islower():
                    out += "_" + s[i].lower()
                else:
                    out += s[i].lower()
            else:
                out += "_" + s[i].lower()
            last_upper=True
        else:
            out += s[i]
            last_upper=False
    return out


NS_TP = "http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"

class Generator(object):

    def __init__(self, quark_list, prefix, basename, opts):
        self.__header = []
        self.__body = []

        self.prefix_lc = prefix.lower()
        self.prefix_uc = prefix.upper()
        self.basename = basename
        self.quark_prefix = opts.get('--quark-prefix', None)

        self.quark_list = [ l.split(None, 2) for l in quark_list ]
            

    def h(self, s):
        self.__header.append(s)

    def b(self, s):
        self.__body.append(s)

    def do_quark(self, quark):
        (qname, qstring) = quark

        self.b('GQuark')
        self.b('%s_%s (void)'
                % (self.prefix_lc, qname.lower()))
        self.b('{')
        self.b('    static GQuark quark = 0;')
        self.b('')
        self.b('    if (G_UNLIKELY (quark == 0))')
        self.b('	quark = g_quark_from_static_string ("%s");'
                % (qstring, ))
        self.b('    return quark;')
        self.b('}')
        self.b('')
        self.b('')

        self.h('#define %s_%s %s_%s()' %
                (self.prefix_uc, qname.upper(),
                 self.prefix_lc, qname.lower()))
        self.h('GQuark %s_%s(void);' % (self.prefix_lc, qname.lower()))
        self.h('')

    def __call__(self):

        self.b('#include "%s.h"' % (self.basename))
        self.b('')

        self.h('#include <glib.h>')
        self.h('')
        self.h('G_BEGIN_DECLS')
        self.h('')

        for quark in self.quark_list:
            self.do_quark(quark)


        self.h('G_END_DECLS')
        self.h('')

        open(self.basename + '.h', 'w').write('\n'.join(self.__header))
        open(self.basename + '.c', 'w').write('\n'.join(self.__body))


def types_to_gtypes(types):
    return [type_to_gtype(t)[1] for t in types]


if __name__ == '__main__':
    options, argv = gnu_getopt(sys.argv[1:], '',
                               ['quark-prefix='])

    opts = {}

    for option, value in options:
        opts[option] = value

    quark_list_file = file(argv[0])
    quark_list = quark_list_file.readlines()
    quark_list_file.close()
    Generator(quark_list, argv[1], argv[2], opts)()
