/**
 * @file fg_argparser.c
 * @brief Command line argument parser
 */

/*
 * Copyright (C) 2014 Felix Rietig <felix.rietig@rwth-aachen.de>
 * Copyright (C) 2006-2013 Antonio Diaz Diaz <antonio@gnu.org>
 *
 * This file is part of Flowgrind.  It is based on the POSIX/GNU
 * command line argument parser 'arg_parser' origninally written by
 * Antonio Diaz Diaz.
 *
 * Flowgrind is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Flowgrind is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Flowgrind.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "fg_argparser.h"

/**
 * Assure at least a minimum size for buffer @p buf
 *
 * @param[in] buf pointer to buffer
 * @param[in] min_size minimum size @p buf should hold in bytes
 */
static void *ap_resize_buffer(void *buf, const int min_size)
{
	if (buf)
		buf = realloc(buf, min_size);
	else
		buf = malloc(min_size);
	return buf;
}

/**
 * Store a parsed option in the state of the arg parser given by @p ap
 *
 * @param[in] ap pointer to the arg parser state
 * @param[in] option pointer to the option to store
 * @param[in] long_opt true if this option was a long option
 * @param[in] argument argument string for this option (may be empty)
 */
static char push_back_record(struct _arg_parser *const ap,
			     const struct _ap_Option *const option,
			     bool long_opt, const char *const argument)
{
	const int len = strlen(argument);
	struct _ap_Record *p;
	void *tmp = ap_resize_buffer(ap->data,
				     (ap->data_size + 1) * sizeof(struct _ap_Record));
	if (!tmp)
		return 0;
	ap->data = (struct _ap_Record *)tmp;
	p = &(ap->data[ap->data_size]);
	p->option = option;
	p->argument = 0;
	tmp = ap_resize_buffer(p->argument, len + 1);
	if (!tmp)
		return 0;
	p->argument = (char *)tmp;
	strncpy(p->argument, argument, len + 1);

	if (long_opt) {
		if (!asprintf(&p->opt_string, "--%s", option->name))
			return 0;
	} else {
		if (!asprintf(&p->opt_string, "-%c", option->code))
			return 0;
	}

	++ap->data_size;
	return 1;
}

/**
 * Add an error message to the arg parser @p ap
 *
 * @param[in] ap pointer to the arg parser state
 * @param[in] msg error string
 */
static char add_error(struct _arg_parser *const ap, const char *const msg)
{
	const int len = strlen(msg);
	void *tmp = ap_resize_buffer(ap->error, ap->error_size + len + 1);
	if (!tmp)
		return 0;
	ap->error = (char *)tmp;
	strncpy(ap->error + ap->error_size, msg, len + 1);
	ap->error_size += len;

	return 1;
}

/**
 * Free all space required by the arg parser @p ap
 *
 * @param[in] ap Pointer to the arg parser state
 */
static void free_data(struct _arg_parser *const ap)
{
	int i;
	for (i = 0; i < ap->data_size; ++i) {
		free(ap->data[i].argument);
		free(ap->data[i].opt_string);
	}
	if (ap->data) {
		free(ap->data);
		ap->data = 0;
	}
	ap->data_size = 0;
}

/**
 * Parses a long option and adds it to the record of arg parser @p ap
 *
 * @param[in] ap pointer to the arg parser state
 * @param[in] opt long option string
 * @param[in] arg option argument string
 * @param[in] options array containing all defined options which may be parsed
 * @param[in] argindp pointer to the index in the command line argument array.
 * The value will be automatically updated
 */
static char parse_long_option(struct _arg_parser *const ap,
			      const char *const opt, const char *const arg,
			      const struct _ap_Option options[],
			      int *const argindp)
{
	unsigned len;
	int index = -1, i;
	char exact = 0, ambig = 0;

	for (len = 0; opt[len + 2] && opt[len + 2] != '='; ++len) ;

	/* Test all long options for either exact match or abbreviated matches. */
	for (i = 0; options[i].code != 0; ++i)
		if (options[i].name
		    && strncmp(options[i].name, &opt[2], len) == 0) {
			/* Exact match found */
			if (strlen(options[i].name) == len) {
				index = i;
				exact = 1;
				break;
			/* First nonexact match found */
			} else if (index < 0) {
				index = i;
			/* Second or later nonexact match found */
			} else if (options[index].code != options[i].code ||
				 options[index].has_arg != options[i].has_arg) {
				ambig = 1;
			}
		}

	if (ambig && !exact) {
		add_error(ap, "option '");
		add_error(ap, opt);
		add_error(ap, "' is ambiguous");
		return 1;
	}

	/* nothing found */
	if (index < 0) {
		add_error(ap, "unrecognized option '");
		add_error(ap, opt);
		add_error(ap, "'");
		return 1;
	}

	++*argindp;

	/* '--<long_option>=<argument>' syntax */
	if (opt[len + 2]) {
		if (options[index].has_arg == ap_no) {
			add_error(ap, "option '--");
			add_error(ap, options[index].name);
			add_error(ap, "' doesn't allow an argument");
			return 1;
		}
		if (options[index].has_arg == ap_yes && !opt[len + 3]) {
			add_error(ap, "option '--");
			add_error(ap, options[index].name);
			add_error(ap, "' requires an argument");
			return 1;
		}
		return push_back_record(ap, &options[index], true, &opt[len + 3]);
	}

	if (options[index].has_arg == ap_yes) {
		if (!arg || !arg[0]) {
			add_error(ap, "option '--");
			add_error(ap, options[index].name);
			add_error(ap, "' requires an argument");
			return 1;
		}
		++*argindp;
		return push_back_record(ap, &options[index], true, arg);
	}

	return push_back_record(ap, &options[index], true, "");
}

/**
 * Parses a short option and adds it to the record of arg parser @p ap
 *
 * @param[in] ap Pointer to the arg parser state
 * @param[in] opt long option string
 * @param[in] arg option argument string
 * @param[in] options array containing all defined options which may be parsed
 * @param[in] argindp pointer to the index in the command line argument array.
 * The value will be automatically updated
 */
static char parse_short_option(struct _arg_parser *const ap,
			       const char *const opt, const char *const arg,
			       const struct _ap_Option options[],
			       int *const argindp)
{
	int cind = 1;	/* character index in opt */

	while (cind > 0) {
		int index = -1, i;
		const unsigned char code = opt[cind];
		char code_str[2];
		code_str[0] = code;
		code_str[1] = 0;

		if (code != 0)
			for (i = 0; options[i].code; ++i)
				if (code == options[i].code) {
					index = i;
					break;
				}

		if (index < 0) {
			add_error(ap, "invalid option -- ");
			add_error(ap, code_str);
			return 1;
		}

		/* opt finished */
		if (opt[++cind] == 0) {
			++*argindp;
			cind = 0;
		}

		if (options[index].has_arg != ap_no && cind > 0 && opt[cind]) {
			if (!push_back_record(ap, &options[index], false, &opt[cind]))
				return 0;
			++*argindp;
			cind = 0;
		} else if (options[index].has_arg == ap_yes) {
			if (!arg || !arg[0]) {
				add_error(ap, "option requires an argument -- ");
				add_error(ap, code_str);
				return 1;
			}
			++*argindp;
			cind = 0;
			if (!push_back_record(ap, &options[index], false, arg))
				return 0;
		} else if (!push_back_record(ap, &options[index], false, "")) {
			return 0;
		}
	}

	return 1;
}

char ap_init(struct _arg_parser *const ap,
	     const int argc, const char *const argv[],
	     const struct _ap_Option options[], const char in_order)
{
	const struct _ap_Option non_option = {0, 0, ap_no, 0};
	const char **non_options = 0;	/* skipped non-options */
	int non_options_size = 0;	/* number of skipped non-options */
	int argind = 1;		/* index in argv */
	int i;

	ap->data = 0;
	ap->error = 0;
	ap->data_size = 0;
	ap->error_size = 0;
	if (argc < 2 || !argv || !options)
		return 1;

	while (argind < argc) {
		const unsigned char ch1 = argv[argind][0];
		const unsigned char ch2 = (ch1 ? argv[argind][1] : 0);

		if (ch1 == '-' && ch2) {	/* we found an option */
			const char *const opt = argv[argind];
			const char *const arg =
			    (argind + 1 < argc) ? argv[argind + 1] : 0;
			if (ch2 == '-') {
				if (!argv[argind][2]) {
					++argind;	/* we found "--" */
					break;
				} else {
				    if (!parse_long_option
					(ap, opt, arg, options, &argind))
					return 0;
				}
			} else {
			    if (!parse_short_option
				(ap, opt, arg, options, &argind))
				return 0;
			}
			if (ap->error)
				break;
		} else {
			if (!in_order) {
				void *tmp = ap_resize_buffer(non_options, (non_options_size + 1) *
							     sizeof *non_options);
				if (!tmp)
					return 0;
				non_options = (const char **)tmp;
				non_options[non_options_size++] = argv[argind++];
			} else if (!push_back_record(ap, &non_option, false, argv[argind++])) {
				return 0;
			}
		}
	}

	if (ap->error) {
		free_data(ap);
	} else {
		for (i = 0; i < non_options_size; ++i)
			if (!push_back_record(ap, &non_option, false, non_options[i]))
				return 0;
		while (argind < argc)
			if (!push_back_record(ap, &non_option, false, argv[argind++]))
				return 0;
	}

	if (non_options)
		free(non_options);
	return 1;
}

void ap_free(struct _arg_parser *const ap)
{
	free_data(ap);
	if (ap->error) {
		free(ap->error);
		ap->error = 0;
	}
	ap->error_size = 0;
}

const char *ap_error(const struct _arg_parser *const ap)
{
	return ap->error;
}

int ap_arguments(const struct _arg_parser *const ap)
{
	return ap->data_size;
}

int ap_code(const struct _arg_parser *const ap, const int i)
{
	if (i >= 0 && i < ap_arguments(ap))
		return ap->data[i].option->code;
	else
		return 0;
}

const char *ap_argument(const struct _arg_parser *const ap, const int i)
{
	if (i >= 0 && i < ap_arguments(ap))
		return ap->data[i].argument;
	else
		return "";
}

const char *ap_opt_string(const struct _arg_parser *const ap, const int i)
{
	if (i >= 0 && i < ap_arguments(ap))
		return ap->data[i].opt_string;
	else
		return "";
}

const struct _ap_Option *ap_option(const struct _arg_parser *const ap,
				   const int i)
{
	if (i >= 0 && i < ap_arguments(ap))
		return ap->data[i].option;
	else
		return 0;
}

bool ap_is_used(const struct _arg_parser *const ap, int code)
{
	bool ret = false;

	for (int i=0; i < ap->data_size; i++)
		if (ap->data[i].option->code == code) {
			ret = true;
			break;
		}

	return ret;
}
