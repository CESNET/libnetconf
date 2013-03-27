#!/usr/bin/python
#
# @file generator.py
# @author David Kupka <dkupka@cesnet.cz>
# @brief Libnetconf transapi generator.
#
# Copyright (c) 2011, CESNET, z.s.p.o.
# All rights reserved.
#
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of the CESNET, z.s.p.o. nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

import argparse
import shutil
import re
import sys

# Use configure.in.template and replace all variables with text
def generate_configure_in(replace, template_dir):
	inf = open (template_dir+'/configure.in', 'r')
	outf = open ('configure.in', 'w')

	for line in inf:
		for pattern, value in replace.items():
			line = line.replace(pattern, value)
		outf.write(line)
	inf.close()
	outf.close()

# Copy source files for autotools
def copy_template_files(name, template_dir):
	shutil.copy2(template_dir+'/specfile.spec.in', name+'.spec.in')
	shutil.copy2(template_dir+'/install-sh', 'install-sh')
	shutil.copy2(template_dir+'/Makefile.in', 'Makefile.in')

# 
def generate_callbacks_file(name, paths):
	# Create or rewrite .c file, will be generated
	outf = open(name+'.c', 'w')

	content = ''
	# License and description
	content += '/*\n'
	content += '* This is automaticaly generated callbacks file\n'
	content += '* It contains 3 parts: Configuration callbacks, RPC callbacks and state data callbacks.\n'
	content += '* Do NOT alter function signatures or any structure untill you exactly know what you are doing.\n'
	content += '*/\n\n'
	# Include header files
	content += '#include <libxml/tree.h>\n'
	content += '#include <libnetconf.h>\n'
	content += '#include <libnetconf/transapi.h>\n'
	content += '\n'
	# Add get state data callback
	content += generate_state_callback()
	# Config callbacks part
	content += generate_config_callbacks(name, paths)

	# Write to file
	outf.write(content)
	outf.close()

def generate_state_callback():
	content = ''
	# function for retrieving state data from device
	content += '/**\n'
	content += ' * @brief Retrieve state data from device and return them as serialized XML'
	content += ' *\n'
	content += ' * @param model\tDevice data model. Serialized YIN.\n'
	content += ' * @param running\tRunning datastore content. Serialized XML.\n'
	content += ' * @param[out] err\tDouble poiter to error structure. Fill error when some occurs.\n'
	content += ' *\n'
	content += ' * @return State data as serialized XML or NULL in case of error.\n'
	content += ' */\n'
	content += 'char * get_state_data (char * model, char * running, struct nc_err **err)\n'
	content += '{\n\treturn NULL;\n}\n\n'

	return(content)


def generate_config_callbacks(name, paths):
	if paths is None:
		paths = ['/']

	callbacks = '\t.callbacks = {'
	funcs_count = 0
	content = ''


	# Add description and instructions
	content += '/*\n'
	content += '* CONFIGURATION callbacks\n'
	content += '* Here follows set of callback functions run every time some change in associated part of running datastore occurs.\n'
	content += '* You can safely modify the bodies of all function as well as add new functions for better lucidity of code.\n'
	content += '*/\n\n'
	# generate callback function for every given sensitive path
	for path in paths:
		path = path.rstrip()
		func_name = 'callback'+re.sub(r'[^\w]', '_', path)
		# first entry in callbacks without coma
		if funcs_count != 0:
			callbacks += ','

		# single entry per generated function
		callbacks += '\n\t\t{.path = "'+path+'", .func = '+func_name+'}'

		# generate function with default doxygen documentation
		content += '/**\n'
		content += ' * @brief This callback will be run when node in path '+path+' changes\n'
		content += ' *\n'
		content += ' * @param op\tObserved change in path. XMLDIFF_OP type.\n'
		content += ' * @param node\tModified node. if op == XMLDIFF_REM its copy of node removed.\n'
		content += ' * @param data\tDouble pointer to void. Its passed to every callback. You can share data using it.\n'
		content += ' *\n'
		content += ' * @return EXIT_SUCCESS or EXIT_FAILURE\n'
		content += ' */\n'
		content += '/* !DO NOT ALTER FUNCTION SIGNATURE! */\n'
		content += 'int '+func_name+' (XMLDIFF_OP op, xmlNodePtr node, void ** data)\n{\n\treturn EXIT_SUCCESS;\n}\n\n'
		funcs_count += 1

	# in the end of file write strucure connecting paths in XML data with callback function
	content += '/*\n'
	content += '* Structure transapi_config_callbacks provide mapping between callback and path in configuration datastore.\n'
	content += '* It is used by libnetconf library to decide which callbacks will be run.\n'
	content += '* DO NOT alter this structure\n'
	content += '*/\n'
	content += 'struct transapi_config_callbacks clbks =  {\n'
	content += '\t.callbacks_count = '+str(funcs_count)+',\n'
	content += '\t.data = NULL,\n'
	content += callbacks+'\n\t}\n'
	content += '};\n\n'

	return(content);


# "main" starts here
parser = argparse.ArgumentParser(description='Generate files for libnetconf transapi callbacks module.')
parser.add_argument('--name', required=True, help='Name of module with callbacks.')
parser.add_argument('--paths', type=argparse.FileType('r'), help='File holding list of sensitive paths in configuration XML.')
parser.add_argument('--template-dir', default='.', help='Path to the directory with teplate files')
try:
	args = parser.parse_args()

	# store paterns and text for replacing in configure.in
	r = {'$$PROJECTNAME$$' : args.name}

	#generate configure.in
	generate_configure_in (r, args.template_dir)
	#copy files for autotools (name.spec.in, Makefile.in, ...)
	copy_template_files(args.name, args.template_dir)
	#generate callbacks code
	generate_callbacks_file(args.name, args.paths, args.model)
except IOError as e:
	print (e[1]+'('+str(e[0])+'): '+e.filename)
except libxml2.libxmlError as e:
	print('Can not parse data model: '+e.msg)
except KeyboardInterrupt:
	print('Killed by user!')

sys.exit(0)

