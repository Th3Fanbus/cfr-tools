/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfr.h"

static int depth = 0;

static void print_tabs(void)
{
	for (int i = 0; i < depth; i++) {
		printf("\t");
	}
}

#define cfr_log(fmt, ...) \
	do { print_tabs(); printf(fmt, ##__VA_ARGS__); } while (0)

#define cfr_log_prop(_prop) \
	do { cfr_log("%s: ", _prop); } while (0)

#define cfr_log_prop_val(_fmt, _prop, _val) \
	do { cfr_log("%-12s ", _prop ":"); printf(_fmt "\n", _val); } while (0)

#define LOG_HEX "0x%x"
#define LOG_NUM "%u"
#define LOG_STR "%s"
#define LOG_SQU "\"%s\""

static void inc_depth(void)
{
	cfr_log("%c\n", '{');
	depth++;
}

static void dec_depth(void)
{
	depth--;
	cfr_log("}%c\n", depth > 0 ? ',' : ';');
}

static const char *tag_to_string(uint32_t tag)
{
	static char buffer[50] = {0};

	switch (tag) {
	case LB_TAG_CFR:			return "Root record";
	case LB_TAG_CFR_OPTION_FORM:		return "Form";
	case LB_TAG_CFR_ENUM_VALUE:		return "Enum value";
	case LB_TAG_CFR_OPTION_ENUM:		return "Enum option";
	case LB_TAG_CFR_OPTION_NUMBER:		return "Number option";
	case LB_TAG_CFR_OPTION_BOOL:		return "Bool option";
	case LB_TAG_CFR_OPTION_VARCHAR:		return "Varchar option";
	case LB_TAG_CFR_VARCHAR_OPT_NAME:	return "Option name";
	case LB_TAG_CFR_VARCHAR_UI_NAME:	return "UI name";
	case LB_TAG_CFR_VARCHAR_UI_HELPTEXT:	return "UI help text";
	case LB_TAG_CFR_VARCHAR_DEF_VALUE:	return "Default value";
	case LB_TAG_CFR_OPTION_COMMENT:		return "Option comment";
	default:
		snprintf(buffer, sizeof(buffer), "UNKNOWN (0x%x)", tag);
		return buffer;
	}
}

static void _print_record(const struct lb_record *rec, const char *f)
{
	const uintptr_t addr = (uintptr_t)(char *)rec;
	if (addr & 0x3) {
		fprintf(stderr, "%s: Address %" PRIxPTR " is not aligned, bailing\n", f, addr);
		exit(-1);
	}

	cfr_log("CFR '%s':\n", tag_to_string(rec->tag));
	cfr_log_prop_val(LOG_HEX, "tag", rec->tag);
	cfr_log_prop_val(LOG_NUM, "size", rec->size);
}

#define print_record(_rec) _print_record((const struct lb_record *)(_rec), __func__)

static bool _tag_neq(const struct lb_record *rec, uint32_t tag, const char *f)
{
	if (rec->tag != tag && tag != LB_TAG_CFR_VARCHAR_UI_HELPTEXT) {
		fprintf(stdout, "%s: expected a '%s' but ", f, tag_to_string(tag));
		fprintf(stdout, "got a '%s' instead\n", tag_to_string(rec->tag));
	}
	return rec->tag != tag;
}

#define tag_mismatch(_rec, _tag) _tag_neq((const struct lb_record *)(_rec), (_tag), __func__)

static void _tag_ok(const struct lb_record *rec, uint32_t tag, const char *f)
{
	if (rec->tag != tag) {
		fprintf(stderr, "%s: expected a '%s' but ", f, tag_to_string(tag));
		fprintf(stderr, "got a '%s' instead, bailing\n", tag_to_string(rec->tag));
		exit(-1);
	}
}

#define ensure_tag_ok(_rec, _tag) _tag_ok((const struct lb_record *)(_rec), (_tag), __func__)

static const char *print_flags(uint32_t flags)
{
	const struct {
		uint32_t flag;
		const char *text;
	} flags_to_text[] = {
		{ CFR_OPTFLAG_READONLY, "read-only"  },
		{ CFR_OPTFLAG_GRAYOUT,  "grayed out" },
		{ CFR_OPTFLAG_SUPPRESS, "suppressed" },
		{ CFR_OPTFLAG_VOLATILE, "volatile"   },
	};

	static char buffer[300] = {0};

	snprintf(buffer, sizeof(buffer), "0x%x (", flags);

	unsigned int num_flags = 0;
	for (unsigned int i = 0; i < ARRAY_SIZE(flags_to_text); i++) {
		if ((flags & flags_to_text[i].flag) == 0) {
			continue;
		}
		if (num_flags != 0) {
			strcat(buffer, ", ");
		}
		strcat(buffer, flags_to_text[i].text);
		num_flags++;
	}
	if (flags == 0) {
		strcat(buffer, "none");
	}
	strcat(buffer, ")");

	return buffer;
}

static uint32_t read_cfr_varchar(char *current, uint32_t tag)
{
	struct lb_cfr_varbinary *cfr_str = (struct lb_cfr_varbinary *)current;

	if (tag_mismatch(cfr_str, tag)) {
		printf("<not found>\n");
		if (tag == LB_TAG_CFR_VARCHAR_UI_HELPTEXT) {
			return 0;
		}
		printf("[HEXDUMP BEGIN]\n");
		for (uint32_t i = 0; i < cfr_str->size; i++) {
			printf("%02x ", ((uint8_t *)current)[i]);
			if ((i & 0xf) == 0xf) {
				printf("\t");
				for (uint32_t j = (i & ~0xf); j <= i; j++) {
					printf("%c", current[j]);
				}
				printf("\n");
			}
		}
		printf("[HEXDUMP END]\n");
		exit(-1);
	}

	printf("\n");
	inc_depth();

	print_record(cfr_str);
	cfr_log_prop_val(LOG_NUM, "data length", cfr_str->data_length);
	cfr_log_prop_val(LOG_SQU, "data", cfr_str->data);

	dec_depth();

	assert(cfr_str->size > cfr_str->data_length);
	return cfr_str->size;
}

static uint32_t sm_read_string_default_value(char *current)
{
	cfr_log_prop("defval");
	return read_cfr_varchar(current, LB_TAG_CFR_VARCHAR_DEF_VALUE);
}

static uint32_t sm_read_opt_name(char *current)
{
	cfr_log_prop("option name");
	return read_cfr_varchar(current, LB_TAG_CFR_VARCHAR_OPT_NAME);
}

static uint32_t sm_read_ui_name(char *current)
{
	cfr_log_prop("UI name");
	return read_cfr_varchar(current, LB_TAG_CFR_VARCHAR_UI_NAME);
}

static uint32_t sm_read_ui_helptext(char *current)
{
	cfr_log_prop("UI help text");
	return read_cfr_varchar(current, LB_TAG_CFR_VARCHAR_UI_HELPTEXT);
}

static uint32_t sm_read_enum_value(char *current)
{
	struct lb_cfr_enum_value *enum_val = (struct lb_cfr_enum_value *)current;
	char *const limit = current + enum_val->size;

	print_record(enum_val);
	cfr_log_prop_val(LOG_NUM, "value", enum_val->value);

	current += sizeof(*enum_val);
	current += sm_read_ui_name(current);

	assert(current == limit);
	return enum_val->size;
}

static uint32_t read_numeric_option(char *current, uint32_t tag)
{
	struct lb_cfr_numeric_option *option = (struct lb_cfr_numeric_option *)current;
	char *const limit = current + option->size;

	ensure_tag_ok(option, tag);

	print_record(option);
	cfr_log_prop_val(LOG_NUM, "object ID", option->object_id);
	cfr_log_prop_val(LOG_STR, "flags", print_flags(option->flags));
	cfr_log_prop_val(LOG_NUM, "defval", option->default_value);

	current += sizeof(*option);
	current += sm_read_opt_name(current);
	current += sm_read_ui_name(current);
	current += sm_read_ui_helptext(current);

	if (option->tag == LB_TAG_CFR_OPTION_ENUM) {
		cfr_log_prop("enum values");
		printf("\n");
		while (current < limit) {
			inc_depth();
			current += sm_read_enum_value(current);
			dec_depth();
		}
	}

	assert(current == limit);
	return option->size;
}

static uint32_t sm_read_opt_enum(char *current)
{
	return read_numeric_option(current, LB_TAG_CFR_OPTION_ENUM);
}

static uint32_t sm_read_opt_number(char *current)
{
	return read_numeric_option(current, LB_TAG_CFR_OPTION_NUMBER);
}

static uint32_t sm_read_opt_bool(char *current)
{
	return read_numeric_option(current, LB_TAG_CFR_OPTION_BOOL);
}

static uint32_t sm_read_opt_varchar(char *current)
{
	struct lb_cfr_varchar_option *option = (struct lb_cfr_varchar_option *)current;
	char *const limit = current + option->size;

	ensure_tag_ok(option, LB_TAG_CFR_OPTION_VARCHAR);

	print_record(option);
	cfr_log_prop_val(LOG_NUM, "object ID", option->object_id);
	cfr_log_prop_val(LOG_STR, "flags", print_flags(option->flags));

	current += sizeof(*option);
	current += sm_read_string_default_value(current);
	current += sm_read_opt_name(current);
	current += sm_read_ui_name(current);
	current += sm_read_ui_helptext(current);

	assert(current == limit);
	return option->size;
}

static uint32_t sm_read_opt_comment(char *current)
{
	struct lb_cfr_option_comment *comment = (struct lb_cfr_option_comment *)current;
	char *const limit = current + comment->size;

	ensure_tag_ok(comment, LB_TAG_CFR_OPTION_COMMENT);

	print_record(comment);
	cfr_log_prop_val(LOG_NUM, "object ID", comment->object_id);
	cfr_log_prop_val(LOG_STR, "flags", print_flags(comment->flags));

	current += sizeof(*comment);
	current += sm_read_ui_name(current);
	current += sm_read_ui_helptext(current);

	assert(current == limit);
	return comment->size;
}

static uint32_t sm_read_object(char *current);

static uint32_t sm_read_form(char *current)
{
	struct lb_cfr_option_form *form = (struct lb_cfr_option_form *)current;
	char *const limit = current + form->size;

	ensure_tag_ok(form, LB_TAG_CFR_OPTION_FORM);

	print_record(form);
	cfr_log_prop_val(LOG_NUM, "object ID", form->object_id);
	cfr_log_prop_val(LOG_STR, "flags", print_flags(form->flags));

	current += sizeof(*form);
	current += sm_read_ui_name(current);

	cfr_log_prop("object list");
	printf("\n");
	while (current < limit) {
		current += sm_read_object(current);
	}

	assert(current == limit);
	return form->size;
}

static uint32_t _sm_read_object(char *current)
{
	struct lb_record *rec = (struct lb_record *)current;

	switch (rec->tag) {
	case LB_TAG_CFR_OPTION_ENUM:
		return sm_read_opt_enum(current);
	case LB_TAG_CFR_OPTION_NUMBER:
		return sm_read_opt_number(current);
	case LB_TAG_CFR_OPTION_BOOL:
		return sm_read_opt_bool(current);
	case LB_TAG_CFR_OPTION_VARCHAR:
		return sm_read_opt_varchar(current);
	case LB_TAG_CFR_OPTION_COMMENT:
		return sm_read_opt_comment(current);
	case LB_TAG_CFR_OPTION_FORM:
		return sm_read_form(current);
	default:
		print_record(rec);
		return rec->size;
	}
}

static uint32_t sm_read_object(char *current)
{
	inc_depth();
	const uint32_t ret = _sm_read_object(current);
	dec_depth();
	return ret;
}

static void sm_read_cfr(char *current)
{
	struct lb_cfr *cfr_root = (struct lb_cfr *)current;
	char *const limit = current + cfr_root->size;

	ensure_tag_ok(cfr_root, LB_TAG_CFR);

	print_record(cfr_root);
	cfr_log_prop_val(LOG_HEX, "checksum", cfr_root->checksum);

	current += sizeof(*cfr_root);

	cfr_log_prop("form list");
	printf("\n");
	while (current < limit) {
		current += sm_read_object(current);
	}

	assert(current == limit);

	printf("length:  %ld\n", (long int)(current - (char *)cfr_root));
	printf("size:    %u\n", cfr_root->size);

	printf("depth:   %d\n", depth);
}

static int alloc_and_read(char **buffer, uint32_t length, struct lb_record *rec, FILE *stream)
{
	rewind(stream);

	*buffer = malloc(length);
	if (!*buffer) {
		fprintf(stderr, "Could not allocate %u bytes\n", length);
		return -1;
	}

	const size_t read_size = fread(*buffer, sizeof((*buffer)[0]), length, stream);

	if (read_size == length) {
		return 0;
	}

	if (feof(stream)) {
		fprintf(stderr, "Unexpected end of file while reading data\n");
	} else if (ferror(stream)) {
		perror("Error reading data");
	} else {
		fprintf(stderr, "Unknown error reading data\n");
	}

	return -1;
}

static int read_from_file(char **buffer, const char *filename)
{
	FILE *stream = fopen(filename, "rb");
	if (!stream) {
		perror("Could not open file");
		return -1;
	}

	int ret = -1;

	struct lb_record record = {0};
	const size_t header_size = fread(&record, sizeof(record), 1, stream);

	if (header_size == 1) {
		if (record.tag == LB_TAG_CFR) {
			ret = alloc_and_read(buffer, record.size, &record, stream);
		} else {
			fprintf(stderr, "Root record tag 0x%x is not a CFR root\n", record.tag);
		}
	} else {
		if (feof(stream)) {
			fprintf(stderr, "Unexpected end of file while reading record\n");
		} else if (ferror(stream)) {
			perror("Error reading record");
		} else {
			fprintf(stderr, "Unknown error reading record\n");
		}
	}

	fclose(stream);
	return ret;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: cfr_read <input file>\n");
		return -1;
	}

	char *buffer = NULL;
	if (read_from_file(&buffer, argv[1])) {
		free(buffer);
		return -1;
	}

	sm_read_cfr(buffer);
	free(buffer);
	return 0;
}
