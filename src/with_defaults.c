/**
 * \file with_defaults.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of NETCONF's with-defaults capability defined in RFC 6243
 *
 * Copyright (C) 2012 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is, and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

#include <stdlib.h>

#include "with_defaults.h"
#include "netconf_internal.h"

static NCDFLT_MODE ncdflt_basic_mode = NCDFLT_MODE_EXPLICIT;
static NCDFLT_MODE ncdflt_supported = (NCDFLT_MODE_ALL
		| NCDFLT_MODE_ALL_TAGGED
		| NCDFLT_MODE_TRIM
		| NCDFLT_MODE_EXPLICIT);

NCDFLT_MODE ncdflt_get_basic_mode()
{
	return (ncdflt_basic_mode);
}

void ncdflt_set_basic_mode(NCDFLT_MODE mode)
{
	/* if one of valid values, change the value */
	if (mode == NCDFLT_MODE_ALL
			|| mode == NCDFLT_MODE_TRIM
			|| mode == NCDFLT_MODE_EXPLICIT) {
		/* set basic mode */
		ncdflt_basic_mode = mode;

		/* if current basic mode is not in supported set, add it */
		if ((ncdflt_supported & ncdflt_basic_mode) == 0) {
			ncdflt_supported |= ncdflt_basic_mode;
		}
	}
}

void ncdflt_set_supported(NCDFLT_MODE modes)
{
	ncdflt_supported = ncdflt_basic_mode;
	ncdflt_supported |= (modes & NCDFLT_MODE_ALL) ? NCDFLT_MODE_ALL : 0;
	ncdflt_supported |= (modes & NCDFLT_MODE_ALL_TAGGED) ? NCDFLT_MODE_ALL_TAGGED : 0;
	ncdflt_supported |= (modes & NCDFLT_MODE_TRIM) ? NCDFLT_MODE_TRIM : 0;
	ncdflt_supported |= (modes & NCDFLT_MODE_EXPLICIT) ? NCDFLT_MODE_EXPLICIT : 0;
}

NCDFLT_MODE ncdflt_get_supported()
{
	return (ncdflt_supported);
}

int ncdflt_rpc_withdefaults(nc_rpc* rpc, NCDFLT_MODE mode)
{
	if (rpc == NULL) {
		return (EXIT_FAILURE);
	}
	if (mode != NCDFLT_MODE_ALL
			&& mode != NCDFLT_MODE_TRIM
			&& mode != NCDFLT_MODE_EXPLICIT
			&& mode != NCDFLT_MODE_ALL_TAGGED) {
		return (EXIT_FAILURE);
	}
	/* all checks passed */
	rpc->with_defaults = mode;
	return (EXIT_SUCCESS);
}

NCDFLT_MODE ncdflt_rpc_get_withdefaults(nc_rpc* rpc)
{
	return (rpc->with_defaults);
}
