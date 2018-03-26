/*
* Copyright 2017,2018  Stacy <stacy@sks.uz>
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#define _GNU_SOURCE	1

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <confuse.h>
#include <symbiont/symerror.h>
#include <symbiont/call_control.h>
#include <symbiont/station_control.h>
#include <symbiont/cfdb.h>
#include <symbiont/cfload.h>
#include <symbiont/filter.h>




static int linesload(cfdb *confdb, struct ccstation *st, cfg_t *stcfg);
static int grpload(cfdb *confdb, cfg_t *cfg);
static int filterload(cfdb *confdb, cfg_t *cfg);
static int stationload(cfdb *confdb, cfg_t *cfg);
static int mkstationgrp(cfdb *confdb, cfg_t *grpcfg);
static struct filter *fltfromcfg(cfg_t *fltcfg);


#define MAX_STRPARAM_LEN	128
static int mkstationgrp(cfdb *confdb, cfg_t *grpcfg)
{
	char	*grpname = NULL;
	int	len;
	char	*hwtype = NULL;
	int	lcn = -1;
	int	ifi_start = -1 ;
	char	*vcprefix = NULL;
	int	vcstart = -1;
	int	bchans = -1;
	int	first = -1;
	int	count = -1;
	bool	autostart = false;
	int	i;
	char	strparam[MAX_STRPARAM_LEN];
	int	total = 0;	
	struct ccstation *st;
	int	res;
	char	*filtername = NULL;
	struct filter *filter = NULL;
	
	assert(confdb);
	assert(grpcfg);	
	grpname = (char *)cfg_title(grpcfg);
	if (!grpname) {
		SYMERROR("unnamed station group in config - skipped\n");
		goto errout;
	}
	len = strlen(grpname);
	if ((len >= MAX_STRPARAM_LEN - 8) || (len <= 0)) {
		SYMERROR("station group %s - name is too long (%d chars max) or empty\n", grpname, MAX_STRPARAM_LEN - 8);
		goto errout;
	}
	lcn = cfg_getint(grpcfg, "lcn");
	if (lcn <= 0) {
		SYMERROR("station group %s - invalid lcn %d (0x%0x)\n", grpname, lcn, lcn);
		goto errout;
	}
	hwtype = cfg_getstr(grpcfg, "hwtype");
	ifi_start = cfg_getint(grpcfg, "ifi_start");
	if (ifi_start <= 0) {
		SYMERROR("station group %s - invalid ifi_start %d\n", grpname, ifi_start);
		goto errout;
	}
	filtername = cfg_getstr(grpcfg, "mmi_filter");
	if (filtername) filter = cfg_lookupname(confdb, CFG_OBJ_FILTER, filtername);
	if ((filtername) && (!filter)) {
		SYMWARNING("filter %s for station group %s is not found - defaults will be unchanged\n",
			filtername, grpname);
	}
	vcprefix = cfg_getstr(grpcfg, "vcprefix");
	if (!vcprefix) {
		SYMERROR("station group %s - no vcprefix, skipped\n", grpname);
		goto errout;
	}
	len = strlen(vcprefix);
	if ((len >= MAX_STRPARAM_LEN - 8) || (len <= 0)) {
		SYMERROR("station group %s -vcprefix is too long (%d chars max) or empty\n", vcprefix, MAX_STRPARAM_LEN - 8);
		goto errout;
	}
	vcstart = cfg_getint(grpcfg, "vcstart");
	if (vcstart <= 0) {
		SYMERROR("station group %s - vcstart %d, must be > 0\n", grpname, vcstart);
		goto errout;
	}
	bchans = cfg_getint(grpcfg, "bchans");
	if (bchans <= 0) {
		SYMERROR("station group %s - invalid bchans %d, must be > 0\n", grpname, bchans);
		goto errout;
	}
	first = cfg_getint(grpcfg, "first");
	if (first <= 0) {
		SYMERROR("station group %s - invalid first station number %d, must be > 0\n", grpname, first);
		goto errout;
	}
	count = cfg_getint(grpcfg, "count");
	if (count <= 0) {
		SYMERROR("station group %s - invalid station count %d, must be > 0\n", grpname, count);
		goto errout;
	}
	
	for (i = 0; i < count; i++) {
		memset(strparam, 0, MAX_STRPARAM_LEN);
		snprintf(strparam, MAX_STRPARAM_LEN - 1 ,"%s/%d", grpname, first + i);
		st = new_ccstation();
		assert(st);
		st->name = strdup(strparam);
		
		memset(strparam, 0, MAX_STRPARAM_LEN);
		snprintf(strparam, MAX_STRPARAM_LEN - 1, "%s%d", vcprefix, vcstart + (bchans * i ));
		
		st->bchan = strdup(strparam);
		st->ifi = ifi_start + i;
		st->autostart = autostart;
		if (filter) {
			st->filter = ref_filter(filter);
		}
		res = cfg_register(confdb, CFG_OBJ_STATION, st);
		if (res == CFDB_OK) ++total;
	}
	SYMDEBUG("group %s, %d stations created ok\n", grpname, total);
	return SYM_OK;
errout:
	return SYM_FAIL;
}


static int grpload(cfdb *confdb, cfg_t *cfg)
{
	int	grpcount, i;
	bool	grp_nonempty = false;
	cfg_t	*gcf;
	int	res;
	int	total = 0;

	assert(confdb);
	assert(cfg);
	grpcount = cfg_size(cfg, "group");
	if (grpcount <= 0) {
		SYMERROR("no station groups defined - nothing to do\n");
		return SYM_FAIL;
	}
/* loading line groups */
	for (i = 0; i < grpcount; i++) {
		gcf = cfg_getnsec(cfg, "group", i);
		if (!gcf) {
			SYMERROR("empty group definition #%d\n", i);
			continue;
		} 
		
		res = mkstationgrp(confdb, gcf);
		if (res != SYM_OK) continue;
		grp_nonempty = true;
		++total;
	}
	if (!grp_nonempty) {
		SYMERROR("all line groups are empty - nothing to do\n");
		return SYM_FAIL;
	};
	SYMDEBUG("%d station groups created\n", total);
	return SYM_OK;
}



static struct filter *fltfromcfg(cfg_t *fltcfg)
{
	bool	sendevt;
	bool	rcvcmd;
	int	ctlcount;
	struct filter *flt;
	char	*name;
	int	i;

	assert(fltcfg);
	name = (char *)cfg_title(fltcfg);
	if (!name) {
		SYMERROR("filter section with no name\n");
		return NULL;
	}
	flt = new_filter();
	assert(flt);
	ctlcount = cfg_size(fltcfg, "ctl");
	flt->name = strdup(name);
	flt->def_sendevt = cfg_getbool(fltcfg, "default_evt");
	flt->def_rcvcmd = cfg_getbool(fltcfg, "default_cmd");
	if (ctlcount <= 0) goto out;
	
	for (i = 0; i < ctlcount; i++) {
		cfg_t	*ctlcfg;
		char	*hwname = NULL;
		char	*alias = NULL;
		bool	sendevt;
		bool	rcvcmd;
		
		ctlcfg = cfg_getnsec(fltcfg, "ctl", i);
		if (!ctlcfg) {
			SYMWARNING("empty ctl definiton in filter \"%s\"\n", name);
			continue;
		}
		hwname = (char *)cfg_title(ctlcfg);
		if (!hwname) {
			SYMWARNING("ctl definiton with no name in filter \"%s\" - skipped\n", name);
			continue;
		}
		alias = cfg_getstr(ctlcfg, "alias");
		sendevt = cfg_getbool(ctlcfg, "evt");
		rcvcmd = cfg_getbool(ctlcfg, "cmd");
		
		(void)flt_add_entry(flt, hwname, alias, sendevt, rcvcmd);
	}
out:
	return flt;
}


static int filterload(cfdb *confdb, cfg_t *cfg)
{
	struct filter *flt;
	int	fltcount, i;
	cfg_t	*fltcfg;
	
	assert(confdb);
	assert(cfg);
	fltcount = cfg_size(cfg, "filter");
	if (fltcount <= 0) {
		SYMDEBUG("No filters defined\n");
		goto out;
	}
	for (i = 0; i < fltcount; i++) {
		fltcfg = cfg_getnsec(cfg, "filter", i);
		if (!fltcfg) {
			SYMWARNING("empty filter section\n");
			continue;
		}
		flt = fltfromcfg(fltcfg);
		if (!flt) continue;
		(void)cfg_register(confdb, CFG_OBJ_FILTER, flt);
	}
out:
	return	SYM_OK;
}


static int stationload(cfdb *confdb, cfg_t *cfg)
{
	struct ccstation *st;
	int	stcount, i, res;
	cfg_t	*stcf;
	char	*name;
	
	assert(confdb);
	assert(cfg);
	stcount = cfg_size(cfg, "station");
	if (stcount <= 0) {
		SYMERROR("no stations defined - nothing to do\n");
		return SYM_FAIL;
	}
	for (i = 0; i < stcount; i++) {
		char	*hwtype = NULL;
		char	*bchan = NULL;
		int	ifi = -1;
		char	*mmi_filter = NULL;
		bool	autostart;
		struct filter *newfilter;
		bool	rehash = false;
		bool	standalone = false;

		stcf = cfg_getnsec(cfg, "station", i);
		if (!stcf) {
			SYMWARNING("empty station definiton #%d - skipped\n", i);
			continue;
		}
		name = (char *)cfg_title(stcf);
		if (!name) {
			SYMWARNING("unnamed station definition #%d - skipped\n", i);
			continue;
		}
		st = cfg_lookupname(confdb, CFG_OBJ_STATION, name);
		if (!st) {
			SYMINFO("station %s is not in a group - no defaults will be applied\n", name);
			st = new_ccstation();
			assert(st);
			st->name = strdup(name);
			rehash = true;
			standalone = true;
		}
		assert(st_isccstation(st));
		
		hwtype = cfg_getstr(stcf, "hwtype");
		bchan = cfg_getstr(stcf, "bchan");
		ifi = cfg_getint(stcf, "ifi");
		mmi_filter = cfg_getstr(stcf, "mmi_filter");
		autostart = cfg_getbool(stcf, "autostart");
		if (autostart != st->autostart) {
			st->autostart = autostart;
		}
		
		if (bchan) {
			if (st->bchan) free(st->bchan);
			st->bchan = strdup(bchan);
			rehash = true;
		}
		if (ifi > 0) {
			st->ifi = ifi;
			rehash = true;
		}
		if (mmi_filter) {
			newfilter = cfg_lookupname(confdb, CFG_OBJ_FILTER, mmi_filter);
			if (newfilter) {
				assert(flt_isfilter(newfilter));
				if (st->filter) (void)free_filter(st->filter);
				st->filter = ref_filter(newfilter);
			} else {
				SYMWARNING("filter %s for station %s is not found - defaults will be unchanged\n",
					mmi_filter, name);
			}
		}
		if (rehash) {
			if (!standalone) (void)cfg_remove(confdb, CFG_OBJ_STATION, st);
			res = cfg_register(confdb, CFG_OBJ_STATION, st);
			assert(res == CFDB_OK);
		}

		(void)linesload(confdb, st, stcf);
	}

	return SYM_OK;
}


#define MAX_LINENAME	256
static int linesload(cfdb *confdb, struct ccstation *st, cfg_t *stcfg)
{
	int	linecount;
	int	i, res;
	
	assert(confdb);
	assert(st);
	assert(stcfg);
	linecount = cfg_size(stcfg, "line");
	if (linecount <= 0) {
		SYMERROR("no lines defined for station %s\n", st->name);
		return SYM_FAIL;
	}
	if (linecount > MAX_LINES) {
		SYMWARNING("too many line for station %s - only the first %d will be configured\n",
			st->name, MAX_LINES);
		linecount = MAX_LINES;
	}
	for (i = 0; i < linecount; i++) {
		char	*name = NULL;
		char	*displayname = NULL;
		bool	evtsend = false;
		bool	cmdrcv	= false;
		bool	xcontrol = true;
		cfg_t	*lcf;
		long int lnumber = -1;
		char	*lnumendptr = NULL;
		struct symline *sl = NULL;
		char	linename[MAX_LINENAME];

		lcf = cfg_getnsec(stcfg, "line", i);
		if (!lcf) {
			SYMERROR("empty line definition #%d for station %s - skipped\n", i, st->name);
			continue;
		}
		
		name = (char *)cfg_title(lcf);
		if (!name) {
			SYMERROR("line #%d definition for station %s has no number - skipped\n", i, st->name);
			continue;
		}
		lnumber = strtol(name, &lnumendptr, 10);
		if (lnumendptr == name) {
			SYMERROR("invalid line number %s for station %s - skipped\n", name, st->name);
			continue;
		}
		if ((lnumber < 1) || (lnumber >= MAX_LINES)) {
			SYMERROR("line number must be in range 1..%d, actual is %d, skipped\n",
					MAX_LINES, lnumber);
			continue;
		}
		name = cfg_getstr(lcf, "name");
		if (!name) {
			SYMERROR("line %d for station %s has no name - skipped\n", i, st->name);
		}
		displayname = cfg_getstr(lcf, "displayname");
		evtsend = cfg_getbool(lcf, "evtsend");
		cmdrcv = cfg_getbool(lcf, "cmdrcv");
		xcontrol = cfg_getbool(lcf, "xcontrol");
		if ((st->lines)[lnumber - 1]) {
			SYMERROR("station %s already has a line %d (%s)\n",
					st->name, lnumber, ((st->lines)[lnumber - 1])->name);
			continue;
		}

		sl = new_symline();
		assert(sl);
		sl->ccst = st;
		memset(linename, 0, MAX_LINENAME);
		snprintf(linename, MAX_LINENAME - 1, "%s/%s", st->name, name);
		sl->name = strdup(linename);
		assert(sl->name);
		sl->number = lnumber - 1;
		if (displayname) sl->displayname = strdup(displayname);
		sl->evtsend = evtsend;
		sl->cmdrcv = cmdrcv;
		sl->xcontrol = xcontrol;
		res = cfg_register(confdb, CFG_OBJ_LINE, sl);
		assert(res == CFDB_OK);
		(st->lines)[lnumber - 1] = sl;
	}
	return SYM_OK;
}

int cfload(cfdb *confdb, struct global_params *gcf, char *configfile)
{

	cfg_opt_t group_opts[] = {
		CFG_STR("hwtype", "8434DX", CFGF_NONE),
		CFG_INT("lcn", 14, CFGF_NONE),
		CFG_INT("ifi_start", 0, CFGF_NODEFAULT),
		CFG_STR("vcprefix", NULL, CFGF_NODEFAULT),
		CFG_STR("mmi_filter", NULL, CFGF_NONE),
		CFG_INT("vcstart", 0, CFGF_NODEFAULT),
		CFG_INT("bchans", 2, CFGF_NONE),
		CFG_INT("first", 0, CFGF_NODEFAULT),
		CFG_INT("count", 0, CFGF_NODEFAULT),
		CFG_BOOL("autostart", true, CFGF_NONE),
		CFG_END()
	};

	cfg_opt_t ctl_opts[] = {
		CFG_STR("alias", NULL, CFGF_NONE),
		CFG_BOOL("evt", false, CFGF_NODEFAULT),
		CFG_BOOL("cmd", false, CFGF_NODEFAULT),
		CFG_END()
	};

	cfg_opt_t filter_opts[] = {
		CFG_BOOL("default_evt", false, CFGF_NONE),
		CFG_BOOL("default_cmd", false, CFGF_NONE),
		CFG_SEC("ctl", ctl_opts, CFGF_TITLE | CFGF_MULTI),
		CFG_END()
	};

	cfg_opt_t line_opts[] = {
		CFG_STR("name", NULL, CFGF_NODEFAULT),
		CFG_STR("displayname", NULL, CFGF_NONE),
		CFG_BOOL("evtsend", false, CFGF_NONE),
		CFG_BOOL("cmdrcv", false, CFGF_NONE),
		CFG_BOOL("xcontrol", true, CFGF_NONE),
		CFG_END()
	};


	cfg_opt_t station_opts[] = {
		CFG_STR("hwtype", "8343DX", CFGF_NONE),
		CFG_STR("bchan", NULL, CFGF_NONE),
		CFG_INT("ifi", -1, CFGF_NONE),
		CFG_STR("mmi_filter", NULL, CFGF_NONE),
		CFG_BOOL("autostart", true, CFGF_NONE),
		CFG_SEC("line", line_opts, CFGF_TITLE | CFGF_MULTI),
		CFG_END()
	};


	cfg_opt_t opts[] = {
		CFG_INT("debug", 3, CFGF_NONE),
		CFG_STR("name", "dcpsym", CFGF_NONE),
		CFG_STR("yxtproto", "unix", CFGF_NONE),
		CFG_STR("hualink", NULL, CFGF_NODEFAULT),
		CFG_STR("yxtlink", NULL, CFGF_NODEFAULT),
		CFG_SEC("group", group_opts, CFGF_MULTI | CFGF_TITLE),
		CFG_SEC("filter", filter_opts, CFGF_MULTI | CFGF_TITLE),
		CFG_SEC("station", station_opts, CFGF_MULTI | CFGF_TITLE),
		CFG_END()
	};

	cfg_t	*cfg;
	int	res;
	char	*tmp;
	
	assert(confdb);
	assert(gcf);
	assert(configfile);
	
	cfg = cfg_init(opts, CFGF_NONE);
	if (!cfg) {
		SYMERROR("cannot initialize config parser\n");
		goto errout;
	}
	res = cfg_parse(cfg, configfile);
	if (res != CFG_SUCCESS) {
		SYMERROR("cannot parse config file %s\n", configfile);
		goto errout;
	}
	
	gcf->debuglevel = cfg_getint(cfg, "debug");
	tmp = cfg_getstr(cfg, "name");
	if (tmp) gcf->symname = strdup(tmp);
	tmp = cfg_getstr(cfg, "hualink");
	if (tmp) gcf->hualink = strdup(tmp);
	tmp = cfg_getstr(cfg, "yxtlink");
	if (tmp) gcf->yxtlink = strdup(tmp);
	tmp = cfg_getstr(cfg, "yxtproto");
	assert(tmp);
	if (strcmp(tmp, "unix") == 0) {
		gcf->yxt_unix = true;
	} else if (strcmp(tmp, "tcp") == 0) {
		gcf->yxt_unix = false;
	} else {
		SYMERROR("unsupported YXT transport protocol \"%s\"\n", tmp);
		goto errout;
	}


	/* loading filter data */
	res = filterload(confdb, cfg);

	/* loading station groups */
	res = grpload(confdb, cfg);

	if (res != SYM_OK) goto errout;

	/* loading stations data */
	res = stationload(confdb, cfg);
	if (res != SYM_OK) goto errout;
	/* free profiles */
	res = SYM_OK;
	goto out;
errout:
	res = SYM_FAIL;
	
out:
	cfg_free(cfg);
	return res;
}

