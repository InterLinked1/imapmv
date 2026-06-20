/*
 * LBBS -- The Lightweight Bulletin Board System
 *
 * Copyright (C) 2026, Naveen Albert
 *
 * Naveen Albert <bbs@phreaknet.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief imapmv -- fast, flexible IMAP message move utility
 *
 * \author Naveen Albert <bbs@phreaknet.org>
 */

#define _GNU_SOURCE /* for memmem */

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>

#include <libetpan/libetpan.h>

/* Global options */
static const char *configpath = NULL;
static const char *rulesfile = NULL;
static int check_only = 0;
static int dryrun = 0;
static int immediate_expunge = 0;
static int folder_limit = 0;

static char host1[512];
static char host2[512];
static char user1[512];
static char user2[512];
static char pass1[512];
static char pass2[512];
static int port1;
static int port2;
static int secure1;
static int secure2;

/* Cumulative counters */
static int total_msg = 0;
static size_t total_bytes = 0;

static int do_abort = 0;

struct imap_client {
	struct mailimap *imap;
	char folder[1024];
};

#define strlen_zero(s) ((!s || *s == '\0'))
#define S_IF(a) S_OR(a, "")
#define S_OR(a, b) ({typeof(&((a)[0])) __x = (a); strlen_zero(__x) ? (b) : __x;})
#define MAILIMAP_ERROR(r) (r != MAILIMAP_NO_ERROR && r != MAILIMAP_NO_ERROR_AUTHENTICATED && r != MAILIMAP_NO_ERROR_NON_AUTHENTICATED)

#define log_mailimap_error(client, code, fmt, ...) log_mailimap(__LINE__, client, code, fmt, ## __VA_ARGS__)
#define log_mailimap_warning(client, code, fmt, ...) log_mailimap(__LINE__, client, code, fmt, ## __VA_ARGS__)

/* maildriver_strerror is like strerror for maildriver, but the codes are completely different for mailimap, so it isn't helpful */
static const char *mailimap_strerror(int code)
{
	switch (code) {
#define MAILIMAP_STRERROR_ITEM(c) case c: return #c
	MAILIMAP_STRERROR_ITEM(MAILIMAP_NO_ERROR);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_NO_ERROR_AUTHENTICATED);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_NO_ERROR_NON_AUTHENTICATED);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_BAD_STATE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_STREAM);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_PARSE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_CONNECTION_REFUSED);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_MEMORY);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_FATAL);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_PROTOCOL);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_DONT_ACCEPT_CONNECTION);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_APPEND);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_NOOP);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_LOGOUT);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_CAPABILITY);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_CHECK);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_CLOSE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_EXPUNGE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_COPY);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_UID_COPY);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_MOVE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_UID_MOVE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_CREATE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_DELETE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_EXAMINE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_FETCH);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_UID_FETCH);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_LIST);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_LOGIN);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_LSUB);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_RENAME);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_SEARCH);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_UID_SEARCH);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_SELECT);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_STATUS);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_STORE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_UID_STORE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_SUBSCRIBE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_UNSUBSCRIBE);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_STARTTLS);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_INVAL);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_EXTENSION);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_SASL);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_SSL);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_NEEDS_MORE_DATA);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_CUSTOM_COMMAND);
	MAILIMAP_STRERROR_ITEM(MAILIMAP_ERROR_CLIENTID);
#undef MAILIMAP_STRERROR_ITEM
	default: return "UNKNOWN";
	}
};

static void __attribute__ ((format (gnu_printf, 4, 5))) log_mailimap(int lineno, struct mailimap *client, int code, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	int last_sent_tag = client->imap_tag;
	struct mailimap_response_info *r = client->imap_response_info;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (r) {
		/* Usually all the fields of r we print here are NULL, but we should have some tag and response info for debugging */
		if (r->rsp_alert || r->rsp_parse || r->rsp_atom || r->rsp_value) {
			fprintf(stderr, "%d: %s [%d: %s]: %d, %s/%s/%s/%s/%s\n",
				lineno, buf, code, mailimap_strerror(code), last_sent_tag, S_IF(client->imap_response), S_IF(r->rsp_alert), S_IF(r->rsp_parse), S_IF(r->rsp_atom), S_IF(r->rsp_value));
		} else {
			fprintf(stderr, "%d: %s [%d: %s]: %d, %s\n",
				lineno, buf, code, mailimap_strerror(code), last_sent_tag, S_IF(client->imap_response));
		}
	} else {
		fprintf(stderr, "%d: %s [%d: %s]\n", lineno, buf, code, mailimap_strerror(code));
	}
}

static int create_client(struct imap_client *c, const char *hostname, int port, int secure, const char *username, const char *password)
{
	int res;
	c->imap = mailimap_new(0, NULL);
	if (!c->imap) {
		return -1;
	}
	if (secure) {
		res = mailimap_ssl_connect(c->imap, hostname, port);
	} else {
		res = mailimap_socket_connect(c->imap, hostname, port);
	}
	if (MAILIMAP_ERROR(res)) {
		log_mailimap_warning(c->imap, res, "Failed to establish IMAP session to %s:%d", hostname, port);
		return -1;
	}

	res = mailimap_login(c->imap, username, password);
	if (MAILIMAP_ERROR(res)) {
		fprintf(stderr, "Failed to login to IMAP server as %s: %s\n", username, S_IF(c->imap->imap_response));
		return -1;
	}

	return 0;
}

static void destroy_client(struct imap_client *c)
{
	if (c->imap) {
		mailimap_free(c->imap);
		c->imap = NULL;
	}
}

static int set_folder(struct imap_client *c, const char *folder)
{
	if (!c->folder[0] || strcmp(c->folder, folder)) {
		int res = mailimap_select(c->imap, folder);
		if (MAILIMAP_ERROR(res)) {
			log_mailimap_warning(c->imap, res, "SELECT '%s' failed", folder);
			return -1;
		}
		snprintf(c->folder, sizeof(c->folder), "%s", folder);
	}
	return 0;
}

/*! \todo upstream? */
static int mailimap_uid_search_raw(mailimap *session, const char *searchkey, clist **outlist)
{
	char cmd[256];
	size_t cmdlen;
	struct mailimap_response *response;
	int r;
	int error_code;
	clist *search_result = NULL;

	if ((session->imap_state != MAILIMAP_STATE_AUTHENTICATED) && (session->imap_state != MAILIMAP_STATE_SELECTED)) {
		return MAILIMAP_ERROR_BAD_STATE;
	}
	r = mailimap_send_current_tag(session);
	if (MAILIMAP_ERROR(r)) {
		return r;
	}

	/* XXX mailimap_send_crlf and mailimap_send_custom_command aren't public */
	cmdlen = (size_t) snprintf(cmd, sizeof(cmd), "UID SEARCH %s\r\n", searchkey);

	r = (int) mailstream_write(session->imap_stream, cmd, cmdlen);
	if (r != (int) cmdlen) {
		return MAILIMAP_ERROR_STREAM;
	}

	if (mailstream_flush(session->imap_stream) == -1) {
		return MAILIMAP_ERROR_STREAM;
	}
	if (mailimap_read_line(session) == NULL) {
		return MAILIMAP_ERROR_STREAM;
	}
	r = mailimap_parse_response(session, &response);
	if (MAILIMAP_ERROR(r)) {
		return r;
	}

	clist_free(session->imap_response_info->rsp_extension_list);
	session->imap_response_info->rsp_extension_list = NULL;

	search_result = session->imap_response_info->rsp_search_result;
	session->imap_response_info->rsp_search_result = NULL;

	/* session->imap_response only contains the last line (e.g. LIST completed) */
	error_code = response->rsp_resp_done->rsp_data.rsp_tagged->rsp_cond_state->rsp_type;
	mailimap_response_free(response);
	*outlist = search_result;

	switch (error_code) {
		case MAILIMAP_RESP_COND_STATE_OK:
			return MAILIMAP_NO_ERROR;
		default:
			return MAILIMAP_ERROR_LIST;
	}
}

static int process_fetch(struct imap_client *c2, const char *f1, const char *f2, struct mailimap_msg_att *msg_att, uint32_t *restrict uid)
{
	int res;
	clistiter *cur;
	char *body = NULL;
	size_t msg_size = 0;
	struct mailimap_flag_list *flag_list = NULL;
	struct mailimap_date_time *dt = NULL;
	const char *hdr = NULL;
	size_t hdrlen = 0;

	/* Note: There may be some leaks here when we return -1, but then we're exiting anyways, so it doesn't really matter */
	for (cur = clist_begin(msg_att->att_list); cur; cur = clist_next(cur)) {
		struct mailimap_msg_att_item *item = clist_content(cur);
		if (item->att_type == MAILIMAP_MSG_ATT_ITEM_STATIC) {
			switch (item->att_data.att_static->att_type) {
				case MAILIMAP_MSG_ATT_UID:
					*uid = item->att_data.att_static->att_data.att_uid;
					break;
				case MAILIMAP_MSG_ATT_INTERNALDATE:
					dt = item->att_data.att_static->att_data.att_internal_date;
					break;
				case MAILIMAP_MSG_ATT_BODY_SECTION:
					msg_size = item->att_data.att_static->att_data.att_body_section->sec_length;
					body = item->att_data.att_static->att_data.att_body_section->sec_body_part;
					/* If filter includes sender, then we use that, otherwise we use subject */
#define STRLEN(s) ( (sizeof(s)/sizeof(s[0])) - sizeof(s[0]) )
#define STARTS_WITH(s, start) (!strncasecmp(s, start, STRLEN(start)))
					if (strstr(f1, "FROM")) {
						if (STARTS_WITH(body, "From: ")) {
							hdr = body;
						} else {
							hdr = memmem(body, msg_size, "\r\nFrom: ", STRLEN("\r\nFrom: "));
							if (hdr) {
								hdr += 2;
							}
						}
					} else {
						if (STARTS_WITH(body, "Subject: ")) {
							hdr = body;
						} else {
							hdr = memmem(body, msg_size, "\r\nSubject: ", STRLEN("\r\nSubject: "));
							if (hdr) {
								hdr += 2;
							}
						}
					}
					if (hdr) {
						const char *hdrend = strstr(hdr, "\r\n");
						if (hdrend) {
							hdrlen = hdrend - hdr;
						} else {
							hdr = "";
						}
					}
					break;
				default:
					fprintf(stderr, "Unhandled FETCH response item\n");
					break;
			}
		} else {
			struct mailimap_msg_att_dynamic *dynamic = item->att_data.att_dyn;
			if (dynamic && dynamic->att_list) {
				clistiter *dcur;
				flag_list = mailimap_flag_list_new_empty();
				if (!flag_list) {
					return -1;
				}
				for (dcur = clist_begin(dynamic->att_list); dcur; dcur = clist_next(dcur)) {
					char *keyword;
					struct mailimap_flag *flag;
					struct mailimap_flag_fetch *flag_fetch = clist_content(dcur);
					switch (flag_fetch->fl_type) {
						case MAILIMAP_FLAG_FETCH_RECENT:
							break;
						case MAILIMAP_FLAG_FETCH_OTHER:
							switch (flag_fetch->fl_flag->fl_type) {
								case MAILIMAP_FLAG_ANSWERED:
								case MAILIMAP_FLAG_FLAGGED:
								case MAILIMAP_FLAG_DELETED:
								case MAILIMAP_FLAG_SEEN:
								case MAILIMAP_FLAG_DRAFT:
									flag = mailimap_flag_new(flag_fetch->fl_flag->fl_type, NULL, NULL);
									res = mailimap_flag_list_add(flag_list, flag);
									if (res != MAILIMAP_NO_ERROR) {
										log_mailimap_warning(c2->imap, res, "FLAG add failed"); /* Technically this came from c1, not c2 */
										mailimap_flag_list_free(flag_list);
										return -1;
									}
									break;
								case MAILIMAP_FLAG_KEYWORD:
									keyword = strdup(flag_fetch->fl_flag->fl_data.fl_keyword);
									if (!keyword) {
										mailimap_flag_list_free(flag_list);
										return -1;
									}
									flag = mailimap_flag_new_flag_keyword(keyword);
									res = mailimap_flag_list_add(flag_list, flag);
									if (res != MAILIMAP_NO_ERROR) {
										log_mailimap_warning(c2->imap, res, "FLAG add failed"); /* Technically this came from c1, not c2 */
										mailimap_flag_list_free(flag_list);
										return -1;
									}
									break;
								case MAILIMAP_FLAG_EXTENSION:
									break;
							}
							break;
					}
				}
			}
		}
	}

	if (!flag_list || !dt || !body || !msg_size) {
		fprintf(stderr, "IMAP server did not return requested information?\n");
		return -1;
	}

	/* Append */
	if (!dryrun) {
		res = mailimap_append(c2->imap, f2, flag_list, dt, body, msg_size);
		mailimap_flag_list_free(flag_list);
		if (MAILIMAP_ERROR(res)) {
			log_mailimap_warning(c2->imap, res, "APPEND %s:%d failed", f1, *uid);
			return -1;
		}
	}

	total_bytes += msg_size;
	total_msg++;
	if (dryrun) {
		fprintf(stdout, "  %s:%d -> %s [%lu] (%.*s) [DRY RUN]\n", f1, *uid, f2, msg_size, (int) hdrlen, S_IF(hdr));
	} else {
		fprintf(stdout, "  %s:%d -> %s [%lu] (%.*s)\n", f1, *uid, f2, msg_size, (int) hdrlen, S_IF(hdr)); /*! \todo Would be nice to include new UID from APPENDUID */
	}
	return 0;
}

static int process_rule(struct imap_client *c1, struct imap_client *c2, const char *f1, const char *f2, const char *filter)
{
	int res;
	clist *uids = NULL;
	clistiter *cur;
	int matched = 0, appended = 0;
	struct mailimap_set *set;
	struct mailimap_fetch_type *fetch_type = NULL;
	struct mailimap_set *appendedset = NULL;
	clist *fetch_result = NULL;

	fprintf(stdout, "%s -> %s (%s)\n", f1, f2, filter);

	/* Change folders upstream if needed */
	if (set_folder(c1, f1)) {
		return -1;
	}

	/* Search for all matching messages */
	res = mailimap_uid_search_raw(c1->imap, filter, &uids);
	if (MAILIMAP_ERROR(res)) {
		if (do_abort) {
			/* On ^C, the IMAP connection often gets interrupted as well (?), often, but not always, here */
			fprintf(stderr, "** Aborted SEARCH due to interrupt\n");
			return 0;
		}
		log_mailimap_warning(c1->imap, res, "SEARCH failed\n");
		return -1;
	}
	if (clist_count(uids) == 0) {
		clist_free(uids);
		return 0; /* Empty set */
	}

	set = mailimap_set_new_empty();
	for (cur = clist_begin(uids); cur; cur = clist_next(cur)) {
		uint32_t *uid = clist_content(cur);
		if (++matched > folder_limit) {
			fprintf(stdout, "    Skipping #%d: %s:%u\n", matched, f1, *uid);
		} else {
			res = mailimap_set_add_single(set, *uid);
			if (MAILIMAP_ERROR(res)) {
				fprintf(stderr, "Failed to add UID to list: %u\n", *uid);
				clist_free(uids);
				return -1;
			}
		}
		free(uid);
	}
	clist_free(uids);

	/* Fetch the matches - all at once to allow for pipelining multiple messages */
	fetch_type = mailimap_fetch_type_new_fetch_att_list_empty();
	mailimap_fetch_type_new_fetch_att_list_add(fetch_type, mailimap_fetch_att_new_uid()); /* UID */
	mailimap_fetch_type_new_fetch_att_list_add(fetch_type, mailimap_fetch_att_new_flags()); /* Flags */
	mailimap_fetch_type_new_fetch_att_list_add(fetch_type, mailimap_fetch_att_new_internaldate()); /* INTERNALDATE */
	mailimap_fetch_type_new_fetch_att_list_add(fetch_type, mailimap_fetch_att_new_body_peek_section(mailimap_section_new(NULL)));

	/*! \todo If dryrun, we don't need to retrieve the whole message, just the headers would be sufficient (to conserve bandwidth) */

	res = mailimap_uid_fetch(c1->imap, set, fetch_type, &fetch_result);
	if (MAILIMAP_ERROR(res)) {
		log_mailimap_warning(c1->imap, res, "FETCH failed");
		/* fetch_result and everything that went into it is already freed */
		mailimap_fetch_type_free(fetch_type);
		mailimap_set_free(set);
		return -1;
	}

	res = 0;
	if (!dryrun) {
		appendedset = mailimap_set_new_empty();
	}
	for (cur = clist_begin(fetch_result); cur; cur = clist_next(cur)) {
		uint32_t uid = 0;
		struct mailimap_msg_att *msg_att = clist_content(cur);
		/* Append the message */
		if (process_fetch(c2, f1, f2, msg_att, &uid)) {
			res = -1;
			break;
		}
		if (!dryrun) {
			/* Mark successfully appended messages for deletion */
			res = mailimap_set_add_single(appendedset, uid);
			if (MAILIMAP_ERROR(res)) {
				break;
			}
			appended++;
		}
		if (do_abort) {
			break;
		}
	}

	if (!dryrun) {
		/* Mark as deleted any messages that were successfully appended (even on failure, so we don't process the same message again next time) */
		if (appended > 0) {
			struct mailimap_flag_list *flag_list = mailimap_flag_list_new_empty();
			res = mailimap_flag_list_add(flag_list, mailimap_flag_new_deleted());
			if (res != MAILIMAP_NO_ERROR) {
				log_mailimap_warning(c1->imap, res, "FLAG add failed");
			} else {
				struct mailimap_store_att_flags *att_flags = mailimap_store_att_flags_new_add_flags_silent(flag_list);
				if (att_flags) {
					res = mailimap_uid_store(c1->imap, appendedset, att_flags);
					if (res != MAILIMAP_NO_ERROR) {
						log_mailimap_warning(c1->imap, res, "UID STORE failed");
					}
					mailimap_store_att_flags_free(att_flags);
				} else {
					res = -1;
				}
				if (immediate_expunge) {
					res = mailimap_expunge(c1->imap);
					if (res != MAILIMAP_NO_ERROR) {
						log_mailimap_warning(c1->imap, res, "EXPUNGE failed");
					}
				}
			}
		}
		mailimap_set_free(appendedset);
	}

	mailimap_fetch_list_free(fetch_result);
	mailimap_fetch_type_free(fetch_type);
	mailimap_set_free(set);
	return res;
}

static int check_rule(struct imap_client *c1, struct imap_client *c2, const char *f1, const char *f2)
{
	if (set_folder(c1, f1)) {
		return -1;
	}
	if (set_folder(c2, f2)) {
		return -1;
	}
	fprintf(stdout, "%s -> %s\n", f1, f2);
	return 0;
}

static int process_opts(int argc, char *argv[])
{
	int c;
	static const char *getopt_settings = "?C:R:cdehl:";

	while ((c = getopt(argc, argv, getopt_settings)) != -1) {
		switch (c) {
		case 'C':
			configpath = optarg;
			break;
		case 'R':
			rulesfile = optarg;
			break;
		case 'c':
			check_only = 1;
			break;
		case 'd':
			dryrun = 1;
			break;
		case 'e':
			immediate_expunge = 1;
			break;
		case 'l':
			folder_limit = atoi(optarg);
			break;
		case 'h':
		case '?':
			printf("imapmv -- fast, flexible IMAP message move utility\n");
			printf("\n");
			printf("Usage: imapmv [-opts[modifiers]] [< filename]\n");
			printf("  -C             INI-style config file for server options, host1, port1, secure1, user1, pass1, host2, port2, secure2, user2, pass2\n");
			printf("  -R             CSV file of rules in format: folder1,folder2,IMAP SEARCH query. If not provided, standard input is used\n");
			printf("  -c             Validate folder names in rules only (on both servers), without moving anything\n");
			printf("  -d             Dry run only, list messages that would be moved but do not actually move them\n");
			printf("  -e             Expunge source folders immediately after moving messages\n");
			printf("  -l             Limit move to this many messages at most PER FOLDER. Use this to limit memory usage;\n");
			printf("                   all matching messages in a folder are normally loaded into memory at once for efficiency.\n");
			printf("  -?             Display this help and exit\n");
			printf("\n");
			printf("Each rule specifies a set of message from folder1 on server1 that will be moved to folder2 on folder2.\n");
			printf("The IMAP SEARCH query specifies which messages will match (ALL to match all, or any other IMAP search expression\n");
			printf("e.g. to archive messages, one might use 'UNDELETED SEEN OLDER 259200', but this will vary based on use case.\n");
			return 1;
		default:
			break;
		}
	}

	return 0;
}

static int load_config(const char *filename)
{
	FILE *fp = fopen(filename, "r");
	if (fp) {
		char buf[512];
		/* Simple config parsing */
		while ((fgets(buf, sizeof(buf), fp))) {
			char *tmp;
			char *key, *val = buf;

			tmp = strchr(buf, '#'); /* Ignore comments */
			if (tmp) {
				*tmp = '\0';
			}
			tmp = strchr(buf, '\n');
			if (tmp) {
				*tmp = '\0';
			}
			tmp = strchr(buf, '\r');
			if (tmp) {
				*tmp = '\0';
			}
			if (strlen_zero(val)) {
				continue;
			}
			/* Trim leading whitespace */
			while (*val == ' ') {
				val++;
			}
			if (strlen_zero(val)) {
				continue;
			}
			key = strsep(&val, "=");
			if (strlen_zero(key)) {
				continue;
			}
			if (!strcmp(key, "host1")) {
				snprintf(host1, sizeof(host1), "%s", val);
			} else if (!strcmp(key, "host2")) {
				snprintf(host2, sizeof(host2), "%s", val);
			} else if (!strcmp(key, "user1")) {
				snprintf(user1, sizeof(user1), "%s", val);
			} else if (!strcmp(key, "user2")) {
				snprintf(user2, sizeof(user2), "%s", val);
			} else if (!strcmp(key, "pass1")) {
				snprintf(pass1, sizeof(pass1), "%s", val);
			} else if (!strcmp(key, "pass2")) {
				snprintf(pass2, sizeof(pass2), "%s", val);
			} else if (!strcmp(key, "port1")) {
				port1 = atoi(val);
			} else if (!strcmp(key, "port2")) {
				port2 = atoi(val);
			} else if (!strcmp(key, "secure1")) {
				secure1 = atoi(val) || tolower(*val) == 'y';
			} else if (!strcmp(key, "secure2")) {
				secure1 = atoi(val) || tolower(*val) == 'y';
			}
		}
		fclose(fp);
	}
	return 0;
}

static int load_settings(int argc, char *argv[], struct imap_client *c1, struct imap_client *c2)
{
	if (process_opts(argc, argv)) {
		return -1;
	}
	if (!configpath) {
		fprintf(stderr, "Missing config path (-C option)\n");
		return -1;
	}
	if (load_config(configpath)) {
		return -1;
	}
	if (create_client(c2, host2, port2, secure2, user2, pass2)) {
		return -1;
	}
	if (create_client(c1, host1, port1, secure1, user1, pass1)) {
		return -1;
	}

	/* Overwrite passwords with garbage, since they're not needed anymore; kludgy, but more portable than using explicit_bzero */
	explicit_bzero(pass1, sizeof(pass1));
	explicit_bzero(pass2, sizeof(pass2));
	return 0;
}

static int process_rules(struct imap_client *c1, struct imap_client *c2)
{
	char buf[8192];
	FILE *fp = NULL;

	if (rulesfile) {
		fp = fopen(rulesfile, "r");
		if (!fp) {
			fprintf(stderr, "Can't open %s: %s\n", rulesfile, strerror(errno));
			return -1;
		}
	} else {
		fp = stdin;
	}

	while ((fgets(buf, sizeof(buf), fp))) {
		char *rule = buf;
		char *f1, *f2, *filter, *tmp;
		if (rule[0] == '#') {
			continue; /* Ignore fully commented lines */
		}
		f1 = strsep(&rule, ",");
		f2 = strsep(&rule, ",");
		filter = rule;
		tmp = strchr(filter, '\n');
		if (!f1 || !f2 || !filter) {
			fprintf(stderr, "Ignoring malformed line\n");
			continue;
		}
		if (tmp) {
			*tmp = '\0';
		}

		if (check_only) {
			if (check_rule(c1, c2, f1, f2)) {
				return -1;
			}
		} else {
			if (process_rule(c1, c2, f1, f2, filter)) {
				return -1;
			}
		}
		if (do_abort) {
			break;
		}
	}
	if (rulesfile) {
		fclose(fp);
	}
	return 0;
}

static void __sigint_handler(int sig)
{
	(void) sig;
	do_abort = 1;
}

static struct sigaction sigint_handler = {
	.sa_handler = __sigint_handler,
};

int main(int argc, char *argv[])
{
	int res = -1;
	struct imap_client c1, c2;

	memset(&c1, 0, sizeof(c1));
	memset(&c2, 0, sizeof(c2));

	if (load_settings(argc, argv, &c1, &c2)) {
		goto cleanup;
	}

	/* Add a signal handler so we cleanly exit on interrupt rather than in the middle of processing a message */
	sigaction(SIGINT, &sigint_handler, NULL);

	if (process_rules(&c1, &c2)) {
		goto cleanup;
	}
	if (do_abort) {
		fprintf(stderr, "** Aborting processing due to ^C interrupt\n");
	}
	if (dryrun) {
		fprintf(stdout, "DRY RUN: %d message%s would have been moved (%lu bytes)\n", total_msg, total_msg == 1 ? "" : "s", total_bytes);
	} else {
		fprintf(stdout, "%d message%s moved (%lu bytes)\n", total_msg, total_msg == 1 ? "" : "s", total_bytes);
	}
	res = 0;

cleanup:
	destroy_client(&c1);
	destroy_client(&c2);
	return res;
}
