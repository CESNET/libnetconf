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

import libxml2
import argparse
import shutil
import re
import sys
import os

transapi_version = 2

target_dir = './'

# Use configure.in.template and replace all variables with text
def generate_configure_in(replace, template_dir, with_libxml2):
	inf = open (template_dir+'/configure.in', 'r')
	outf = open (target_dir+'/configure.in', 'w')

	conf_in = inf.read()
	for pattern, value in replace.items():
		if re.match(r'\$\$LIBXML2_.*\$\$', pattern) and with_libxml2 == 0:
			conf_in = conf_in.replace(pattern, '')
		else:
			conf_in = conf_in.replace(pattern, value)

	outf.write(conf_in)
	inf.close()
	outf.close()

# Copy source files for autotools
def copy_template_files(name, template_dir):
	shutil.copy2(template_dir+'/install-sh', target_dir+'/install-sh')
	shutil.copy2(template_dir+'/config.guess', target_dir+'/config.guess')
	shutil.copy2(template_dir+'/config.sub', target_dir+'/config.sub')
	shutil.copy2(template_dir+'/ltmain.sh', target_dir+'/ltmain.sh')
	shutil.copy2(template_dir+'/Makefile.in', target_dir+'/Makefile.in')

def separate_paths_and_namespaces(defs):
	paths = []
	namespaces = []
	for d in defs:
		d = d.rstrip()
		# skip empty lines and lines starting with '#' (bash/python style single line comments)
		if len(d) == 0 or d[0] == '#':
			continue

		# path definition
		if re.match(r'(/([\w]+:)?[\w]+)+', d):
			paths.append(d)
		elif re.match(r'[\w]+=.+', d):
			namespaces.append(d.split('='))
		else:
			raise ValueError('Line '+d+' is not valid namespace definition nor XPath.')

	return (paths,namespaces)

# 
def generate_callbacks_file(name, defs, model, with_libxml2, without_init, without_close):
	if defs is None:
		raise ValueError('Invalid paths file.')
	# Create or rewrite .c file, will be generated
	outf = open(target_dir+'/'+name+'.c', 'w')

	content = ''
	# License and description
	content += '/*\n'
	content += ' * This is automaticaly generated callbacks file\n'
	content += ' * It contains 3 parts: Configuration callbacks, RPC callbacks and state data callbacks.\n'
	content += ' * Do NOT alter function signatures or any structures unless you know exactly what you are doing.\n'
	content += ' */\n\n'
	# Include header files
	content += '#include <stdlib.h>\n'
	if with_libxml2:
		content += '#include <libxml/tree.h>\n'
		content += '#include <libnetconf_xml.h>\n'
	else:
		content += '#include <libnetconf.h>\n'
	content += '\n'
	# transAPI version
	content += '/* transAPI version which must be compatible with libnetconf */\n'
	content += 'int transapi_version = '+str(transapi_version)+';\n\n'
	# libxml2?
	content += '/* Determines whether XML arguments are passed as (xmlDocPtr) or (char *). */\n'
	content += 'int with_libxml2 = '+str(with_libxml2)+';\n'
	content += '\n/*\n'
	content += ' * Signal to libnetconf that configuration data were modified by any callback.\n'
	content += ' * 0 - data not modified\n'
	content += ' * 1 - data have been modified\n'
	content += ' */\n'
	content += 'int config_modified = 0;\n\n'
	# init and close functions 
	if not (without_init):
		content += generate_init_callback(with_libxml2)
	if not (without_close):
		content += generate_close_callback(with_libxml2)
	# Add get state data callback
	content += generate_state_callback(with_libxml2)
	# Config callbacks part
	(paths, namespaces) = separate_paths_and_namespaces(defs)
	content += generate_config_callbacks(name, paths, namespaces, with_libxml2)
	# RPC callbacks part
	if not (model is None):
		content += generate_rpc_callbacks(model, with_libxml2)

	# Write to file
	outf.write(content)
	outf.close()

def generate_init_callback(with_libxml2):
    content = '';
    content += '/**\n'
    content += ' * @brief Initialize plugin after loaded and before any other functions are called.\n'
    content += ' *\n'
    content += ' * @return EXIT_SUCCESS or EXIT_FAILURE\n'
    content += ' */\n'
    content += 'int transapi_init(void)\n'
    content += '{\n\treturn EXIT_SUCCESS;\n}\n\n'

    return (content)
    
def generate_close_callback(with_libxml2):
    content = ''
    content += '/**\n'
    content += ' * @brief Free all resources allocated on plugin runtime and prepare plugin for removal.\n'
    content += ' */\n' 
    content += 'void transapi_close(void)\n'
    content += '{\n\treturn;\n}\n\n'
    
    return (content)

def generate_state_callback(with_libxml2):
	content = ''
	# function for retrieving state data from device
	content += '/**\n'
	content += ' * @brief Retrieve state data from device and return them as serialized XML\n'
	content += ' *\n'
	#if with_libxml2:
	#	content += ' * @param model\tDevice data model. libxml2 xmlDocPtr.\n'
	#	content += ' * @param running\tRunning datastore content. libxml2 xmlDocPtr.\n'
	#else:
	content += ' * @param model\tDevice data model. Serialized YIN.\n'
	content += ' * @param running\tRunning datastore content. Serialized XML.\n'
	content += ' * @param[out] err\tDouble poiter to error structure. Fill error when some occurs.\n'
	content += ' *\n'
	#if with_libxml2:
	#	content += ' * @return State data as libxml2 xmlDocPtr or NULL in case of error.\n'
	#else:
	content += ' * @return State data as serialized XML or NULL in case of error.\n'
	content += ' */\n'
	#if with_libxml2:
	#	content += 'xmlDocPtr get_state_data (xmlDocPtr model, xmlDocPtr running, struct nc_err **err)\n'
	#else:
	content += 'char * get_state_data (char * model, char * running, struct nc_err **err)\n'
	content += '{\n\treturn NULL;\n}\n\n'

	return(content)

def generate_config_callbacks(name, paths, namespaces, with_libxml2):
	if paths is None:
		raise ValueError('At least one path is required.')

	content = ''
	callbacks = '\t.callbacks = {'
	funcs_count = 0

	# prefix to uri mapping 
	content += '/*\n'
	content += ' * Mapping prefixes with namespaces.\n'
	content += ' * Do NOT modify this structure!\n'
	content += ' */\n'
	namespace = 'char * namespace_mapping[] = {'
	for ns in namespaces:
		namespace += '"'+ns[0]+'", "'+ns[1]+'", '

	content += namespace +'NULL, NULL};\n'
	content += '\n'

	# Add description and instructions
	content += '/*\n'
	content += '* CONFIGURATION callbacks\n'
	content += '* Here follows set of callback functions run every time some change in associated part of running datastore occurs.\n'
	content += '* You can safely modify the bodies of all function as well as add new functions for better lucidity of code.\n'
	content += '*/\n\n'
	# generate callback function for every given sensitive path
	for path in paths:
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
		if with_libxml2:
			content += 'int '+func_name+' (XMLDIFF_OP op, xmlNodePtr node, void ** data)\n{\n\treturn EXIT_SUCCESS;\n}\n\n'
		else:
			content += 'int '+func_name+' (XMLDIFF_OP op, char * node, void ** data)\n{\n\treturn EXIT_SUCCESS;\n}\n\n'
		funcs_count += 1

	# in the end of file write strucure connecting paths in XML data with callback function
	content += '/*\n'
	content += '* Structure transapi_config_callbacks provide mapping between callback and path in configuration datastore.\n'
	content += '* It is used by libnetconf library to decide which callbacks will be run.\n'
	content += '* DO NOT alter this structure\n'
	content += '*/\n'
	if with_libxml2:
		content += 'struct transapi_xml_data_callbacks clbks =  {\n'
	else:
		content += 'struct transapi_data_callbacks clbks =  {\n'
	content += '\t.callbacks_count = '+str(funcs_count)+',\n'
	content += '\t.data = NULL,\n'
	content += callbacks+'\n\t}\n'
	content += '};\n\n'

	return(content);

def generate_rpc_callbacks (doc, with_libxml2):
	content = ''
	callbacks = ''

	# Add description and instructions
	content += '/*\n'
	content += '* RPC callbacks\n'
	content += '* Here follows set of callback functions run every time RPC specific for this device arrives.\n'
	content += '* You can safely modify the bodies of all function as well as add new functions for better lucidity of code.\n'
	content += '* Every function takes array of inputs as an argument. On few first lines they are assigned to named variables. Avoid accessing the array directly.\n'
	content += '* If input was not set in RPC message argument in set to NULL.\n'
	content += '*/\n\n'

	# create xpath context
	ctxt = doc.xpathNewContext()
	ctxt.xpathRegisterNs('yang', 'urn:ietf:params:xml:ns:yang:yin:1')

	# find all RPC defined in data model
	rpcs = ctxt.xpathEval('//yang:rpc')

	# for every RPC
	for rpc in rpcs:
		rpc_name = rpc.prop('name')
		# create callback function
		if with_libxml2:
			rpc_function = 'nc_reply * rpc_'+re.sub(r'[^\w]', '_', rpc_name)+' (xmlNodePtr input[])\n{\n'
		else:
			rpc_function = 'nc_reply * rpc_'+re.sub(r'[^\w]', '_', rpc_name)+' (char *input[])\n{\n'
		# find all defined inputs
		rpc_input = ctxt.xpathEval('//yang:rpc[@name="'+rpc.prop('name')+'"]/yang:input/*')
		arg_order = '{'
		for inp in rpc_input:
			# assign inputs to named variables
			if with_libxml2:
				rpc_function += '\txmlNodePtr '+re.sub(r'[^\w]', '_', inp.prop('name'))+' = input['+str(rpc_input.index(inp))+'];\n'
			else:
				rpc_function += '\tchar * '+re.sub(r'[^\w]', '_', inp.prop('name'))+' = input['+str(rpc_input.index(inp))+'];\n'
			if not inp is rpc_input[0]:
				arg_order += ', '
			arg_order += '"'+inp.prop('name')+'"'

		arg_order += '}'
		rpc_function += '\n\treturn NULL; \n}\n'
		content += rpc_function

		if not rpc is rpcs[0]:
			callbacks += ','
		# add connection between callback and RPC message and order of arguments passed to the callback
		callbacks += '\n\t\t{.name="'+rpc_name+'", .func=rpc_'+re.sub(r'[^\w]', '_', rpc_name)+', .arg_count='+str(len(rpc_input))+', .arg_order='+arg_order+'}'

	content += '/*\n'
	content += '* Structure transapi_rpc_callbacks provide mapping between callbacks and RPC messages.\n'
	content += '* It is used by libnetconf library to decide which callbacks will be run when RPC arrives.\n'
	content += '* DO NOT alter this structure\n'
	content += '*/\n'
	if with_libxml2:
		content += 'struct transapi_xml_rpc_callbacks rpc_clbks = {\n'
	else:
		content += 'struct transapi_rpc_callbacks rpc_clbks = {\n'
	content += '\t.callbacks_count = '+str(len(rpcs))+',\n'
	content += '\t.callbacks = {'+callbacks+'\n\t}'
	content += '\n};\n\n'

	return(content)

# Try to find template directory, if none of known candidates found raise exception
def find_templates():
	known_paths = ['/usr/share/libnetconf/templates/', '/usr/local/share/libnetconf/templates/', './templates/', './']

	for path in known_paths:
		if os.path.isdir(path):
			if os.path.exists(path+'install-sh') and os.path.exists(path+'Makefile.in') and \
					os.path.exists(path+'config.guess') and os.path.exists(path+'config.sub') and \
					os.path.exists(path+'ltmain.sh'):
				return(path)
	
	raise Exception('Template directory not found. Use --template-dir parameter to specify its location.')

# "main" starts here
parser = argparse.ArgumentParser(description='Generate files for libnetconf transapi callbacks module.')
parser.add_argument('--name', required=True, help='Name of module with callbacks.')
parser.add_argument('--paths', type=argparse.FileType('r'), help='File holding list of sensitive paths in configuration XML.')
parser.add_argument('--model', type=libxml2.parseFile, help='File holding data model. Used for generating rpc callbacks.')
parser.add_argument('--template-dir', default=None, help='Path to the directory with teplate files')
parser.add_argument('--with-libxml2', action='store_const', const=1, default=0)
parser.add_argument('--without-init', action='store_const', const=1, default=0, help='Module does not need initialization when loaded.')
parser.add_argument('--without-close', action='store_const', const=1, default=0, help='Module does not need closing before unloaded.')
try:
	args = parser.parse_args()

	# if --template-dir not specified try to find it
	# Would be nicer to call this function in 'default' part of parsing argument
	# --template-dir but then it gets called before trying to find and parse argument :(
	if args.template_dir is None:
		args.template_dir = find_templates()
	# store paterns and text for replacing in configure.in
	r = {'$$PROJECTNAME$$' : args.name, 
	'$$LIBXML2_WITH$$' :
'\n# --with-libxml2=path-to-libxml2-git-repository\nAC_ARG_WITH([libxml2],\n\
\t[AC_HELP_STRING([--with-libxml2], [specific libxml2 location])],\n\
\t[\n\
\t\tAC_CHECK_PROG([XML2_CONFIG], [xml2-config], [yes], [no], [$withval])\n\
\t\tif test "$XML2_CONFIG" = "no"; then\n\
\t\t\tAC_MSG_ERROR([Missing development package of libxml2.])\n\
\t\tfi\n\
\t\tCFLAGS="`$withval/xml2-config --cflags` $CFLAGS"\n\
\t\tLDFLAGS="`$withval/xml2-config --libs` $LDFLAGS"\n\
\t\tWITH_LIBXML2="$withval"\n\
\t]\n)\n',
	'$$LIBXML2_CHECK$$' :
'\n# Check for libxml2.\n\
if test -z "$WITH_LIBXML2" ; then\n\
\tAC_CHECK_PROG([XML2_CONFIG], [xml2-config], [yes], [no])\n\
\tif test "$XML2_CONFIG" = "no"; then\n\
\t\tAC_MSG_ERROR([Missing development package of libxml2.])\n\
\tfi\n\
\tAC_CHECK_LIB([xml2], [main], [LIBS="`xml2-config --libs` $LIBS" CFLAGS="`xml2-config --cflags` $CFLAGS"], AC_MSG_ERROR([Libxml2 not found ]))\n\
fi\n\n'
	}
	# prepare output directory
	target_dir = './'+args.name
	os.mkdir(target_dir)

	#generate configure.in
	generate_configure_in (r, args.template_dir, args.with_libxml2)
	#copy files for autotools (Makefile.in, ...)
	copy_template_files(args.name, args.template_dir)
	#generate callbacks code
	generate_callbacks_file(args.name, args.paths, args.model, args.with_libxml2, args.without_init, args.without_close)
except ValueError as e:
	print (e)
except IOError as e:
	print (e[1]+'('+str(e[0])+'): '+e.filename)
except libxml2.libxmlError as e:
	print('Can not parse data model: '+e.msg)
except KeyboardInterrupt:
	print('Killed by user!')
except Exception as e:
	print(str(e[0]))

sys.exit(0)

