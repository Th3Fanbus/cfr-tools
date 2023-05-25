/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfr.h"

static FILE *ostream = NULL;

static int depth = 0;

static void print_tabs(FILE *stream)
{
	for (int i = 0; i < depth; i++) {
		fprintf(stream, "\t");
	}
}

#define LOG_HEX "0x%x"
#define LOG_H32 "0x%08x"
#define LOG_NUM "%u"
#define LOG_STR "%s"
#define LOG_SQU "'%s'"

#define hprintf(stream, ...) \
	do { print_tabs(stream); fprintf(stream, ##__VA_ARGS__); } while (0)

#define hprintln(stream, ...) \
	do { hprintf(stream, ##__VA_ARGS__); fprintf(stream, "\n"); } while (0)

static void hpropval(FILE *stream, const char *fmt, const char *prop, uint32_t val)
{
	static char buf[200] = {0};

	/* This can blow up if fmt does not have exactly one format specifier */
	snprintf(buf, sizeof(buf),
		"<input type='text' name='%%s' value='%s' readonly>", fmt);

	hprintln(stream, "<label>%s", prop);
	depth++;
	hprintln(stream, buf, prop, val);
	depth--;
	hprintln(stream, "</label>");
}

static bool _tag_neq(const struct lb_record *rec, uint32_t tag, const char *f)
{
	if (rec->tag != tag && tag != LB_TAG_CFR_VARCHAR_UI_HELPTEXT) {
		fprintf(stderr, "%s: expected tag 0x%x but ", f, tag);
		fprintf(stderr, "got tag 0x%x instead\n", rec->tag);
	}
	return rec->tag != tag;
}

#define tag_mismatch(_rec, _tag) _tag_neq((const struct lb_record *)(_rec), (_tag), __func__)

static void _tag_ok(const struct lb_record *rec, uint32_t tag, const char *f)
{
	if (rec->tag != tag) {
		fprintf(stderr, "%s: expected tag 0x%x but ", f, tag);
		fprintf(stderr, "got tag 0x%x instead, bailing\n", rec->tag);
		exit(-1);
	}
}

#define ensure_tag_ok(_rec, _tag) _tag_ok((const struct lb_record *)(_rec), (_tag), __func__)

static const char *print_flags(uint32_t flags)
{
	/* This is only accurate from a visual standpoint. It won't work properly. */
	const struct {
		uint32_t flag;
		const char *text;
	} flags_to_text[] = {
		{ CFR_OPTFLAG_READONLY, " readonly" },
		{ CFR_OPTFLAG_GRAYOUT,  " disabled" },
		{ CFR_OPTFLAG_SUPPRESS, " hidden"   },
		{ CFR_OPTFLAG_VOLATILE, ""          },
	};

	static char buffer[300] = {0};
	memset(buffer, 0, sizeof(buffer));

	for (unsigned int i = 0; i < ARRAY_SIZE(flags_to_text); i++) {
		if ((flags & flags_to_text[i].flag) == 0) {
			continue;
		}
		strcat(buffer, flags_to_text[i].text);
	}
	return buffer;
}

static uint32_t read_cfr_varchar(char **out, char *current, uint32_t tag)
{
	struct lb_cfr_varbinary *cfr_str = (struct lb_cfr_varbinary *)current;

	if (tag_mismatch(cfr_str, tag)) {
		if (tag == LB_TAG_CFR_VARCHAR_UI_HELPTEXT) {
			*out = strdup("");
			if (!*out) {
				fprintf(stderr, "Could not 'strdup(\"\")'\n");
				exit(-1);
			}
			return 0;
		}
		fprintf(stderr, "Could not find required varchar with tag 0x%x\n", tag);
		exit(-1);
	}

	*out = malloc(cfr_str->data_length);
	if (!*out) {
		fprintf(stderr, "Could not allocate %u bytes for CFR string '%s'\n",
			cfr_str->data_length, cfr_str->data);
		exit(-1);
	}
	snprintf(*out, cfr_str->data_length, "%s", cfr_str->data);

	assert(strlen(*out) + 1 == cfr_str->data_length);
	assert(cfr_str->size > cfr_str->data_length);
	return cfr_str->size;
}

char *fixme;

static uint32_t sm_read_string_default_value(char **out, char *current)
{
	return read_cfr_varchar(out, current, LB_TAG_CFR_VARCHAR_DEF_VALUE);
}

static uint32_t sm_read_opt_name(char **out, char *current)
{
	return read_cfr_varchar(out, current, LB_TAG_CFR_VARCHAR_OPT_NAME);
}

static uint32_t sm_read_ui_name(char **out, char *current)
{
	return read_cfr_varchar(out, current, LB_TAG_CFR_VARCHAR_UI_NAME);
}

static uint32_t sm_read_ui_helptext(char **out, char *current)
{
	return read_cfr_varchar(out, current, LB_TAG_CFR_VARCHAR_UI_HELPTEXT);
}

static uint32_t sm_read_enum_value(char *current, uint32_t default_value)
{
	struct lb_cfr_enum_value *enum_val = (struct lb_cfr_enum_value *)current;
	char *const limit = current + enum_val->size;

	ensure_tag_ok(enum_val, LB_TAG_CFR_ENUM_VALUE);

	char *ui_name = NULL;

	current += sizeof(*enum_val);
	current += sm_read_ui_name(&ui_name, current);
	assert(ui_name);

	const char *selected = (enum_val->value == default_value) ? " selected" : "";

	hprintln(ostream, "<option value='%u'%s>%s</option>",
		enum_val->value, selected, ui_name);

	assert(current == limit);
	return enum_val->size;
}

static uint32_t sm_read_opt_enum(char *current)
{
	struct lb_cfr_numeric_option *option = (struct lb_cfr_numeric_option *)current;
	char *const limit = current + option->size;

	char *opt_name = NULL;
	char *ui_name = NULL;
	char *ui_helptext = NULL;

	ensure_tag_ok(option, LB_TAG_CFR_OPTION_ENUM);

	current += sizeof(*option);
	current += sm_read_opt_name(&opt_name, current);
	current += sm_read_ui_name(&ui_name, current);
	current += sm_read_ui_helptext(&ui_helptext, current);
	assert(opt_name);
	assert(ui_name);
	assert(ui_helptext);

	hprintln(ostream, "<td class='ui-name'>");
	depth++;
	hprintln(ostream, "<label for='object-%u'>%s</label>", option->object_id, ui_name);
	depth--;
	hprintln(ostream, "</td>");
	hprintln(ostream, "<td class='ui-input'>");
	depth++;
	hprintln(ostream, "<select id='object-%u' name='%s'%s>",
		option->object_id, opt_name, print_flags(option->flags));
	depth++;
	while (current < limit) {
		current += sm_read_enum_value(current, option->default_value);
	}
	depth--;
	hprintln(ostream, "</select>");
	depth--;
	hprintln(ostream, "</td>");
	hprintln(ostream, "<td>");
	depth++;
	hprintln(ostream, "<span>%s</span>", ui_helptext);
	depth--;
	hprintln(ostream, "</td>");

	assert(current == limit);
	return option->size;
}

static uint32_t sm_read_opt_number(char *current)
{
	struct lb_cfr_numeric_option *option = (struct lb_cfr_numeric_option *)current;
	char *const limit = current + option->size;

	char *opt_name = NULL;
	char *ui_name = NULL;
	char *ui_helptext = NULL;

	ensure_tag_ok(option, LB_TAG_CFR_OPTION_NUMBER);

	current += sizeof(*option);
	current += sm_read_opt_name(&opt_name, current);
	current += sm_read_ui_name(&ui_name, current);
	current += sm_read_ui_helptext(&ui_helptext, current);
	assert(opt_name);
	assert(ui_name);
	assert(ui_helptext);

	hprintln(ostream, "<td class='ui-name'>");
	depth++;
	hprintln(ostream, "<label for='object-%u'>%s</label>", option->object_id, ui_name);
	depth--;
	hprintln(ostream, "</td>");
	hprintln(ostream, "<td class='ui-input'>");
	depth++;
	hprintln(ostream, "<input type='number' id='object-%u' name='%s' value='%u'%s>",
		option->object_id, opt_name, option->default_value, print_flags(option->flags));
	depth--;
	hprintln(ostream, "</td>");
	hprintln(ostream, "<td>");
	depth++;
	hprintln(ostream, "<span>%s</span>", ui_helptext);
	depth--;
	hprintln(ostream, "</td>");

	assert(current == limit);
	return option->size;
}

static uint32_t sm_read_opt_bool(char *current)
{
	struct lb_cfr_numeric_option *option = (struct lb_cfr_numeric_option *)current;
	char *const limit = current + option->size;

	char *opt_name = NULL;
	char *ui_name = NULL;
	char *ui_helptext = NULL;

	ensure_tag_ok(option, LB_TAG_CFR_OPTION_BOOL);

	current += sizeof(*option);
	current += sm_read_opt_name(&opt_name, current);
	current += sm_read_ui_name(&ui_name, current);
	current += sm_read_ui_helptext(&ui_helptext, current);
	assert(opt_name);
	assert(ui_name);
	assert(ui_helptext);

	const char *checked = option->default_value ? " checked" : "";

	hprintln(ostream, "<td class='ui-name'>");
	depth++;
	hprintln(ostream, "<label for='object-%u'>%s</label>", option->object_id, ui_name);
	depth--;
	hprintln(ostream, "</td>");
	hprintln(ostream, "<td class='ui-input'>");
	depth++;
	hprintln(ostream, "<input type='checkbox' id='object-%u' name='%s'%s%s>",
		option->object_id, opt_name, checked, print_flags(option->flags));
	depth--;
	hprintln(ostream, "</td>");
	hprintln(ostream, "<td>");
	depth++;
	hprintln(ostream, "<span>%s</span>", ui_helptext);
	depth--;
	hprintln(ostream, "</td>");

	assert(current == limit);
	return option->size;
}

static uint32_t sm_read_opt_varchar(char *current)
{
	struct lb_cfr_varchar_option *option = (struct lb_cfr_varchar_option *)current;
	char *const limit = current + option->size;

	char *opt_name = NULL;
	char *ui_name = NULL;
	char *ui_helptext = NULL;
	char *default_value = NULL;

	ensure_tag_ok(option, LB_TAG_CFR_OPTION_VARCHAR);

	current += sizeof(*option);
	current += sm_read_string_default_value(&default_value, current);
	current += sm_read_opt_name(&opt_name, current);
	current += sm_read_ui_name(&ui_name, current);
	current += sm_read_ui_helptext(&ui_helptext, current);
	assert(opt_name);
	assert(ui_name);
	assert(ui_helptext);
	assert(default_value);

	hprintln(ostream, "<td class='ui-name'>");
	depth++;
	hprintln(ostream, "<label for='object-%u'>%s</label>", option->object_id, ui_name);
	depth--;
	hprintln(ostream, "</td>");
	hprintln(ostream, "<td class='ui-input'>");
	depth++;
	hprintln(ostream, "<input type='text' id='object-%u' name='%s' value='%s'%s>",
		option->object_id, opt_name, default_value, print_flags(option->flags));
	depth--;
	hprintln(ostream, "</td>");
	hprintln(ostream, "<td>");
	depth++;
	hprintln(ostream, "<span>%s</span>", ui_helptext);
	depth--;
	hprintln(ostream, "</td>");

	assert(current == limit);
	return option->size;
}

static uint32_t sm_read_opt_comment(char *current)
{
	struct lb_cfr_option_comment *comment = (struct lb_cfr_option_comment *)current;
	char *const limit = current + comment->size;

	char *ui_name = NULL;
	char *ui_helptext = NULL;

	ensure_tag_ok(comment, LB_TAG_CFR_OPTION_COMMENT);

	current += sizeof(*comment);
	current += sm_read_ui_name(&ui_name, current);
	current += sm_read_ui_helptext(&ui_helptext, current);
	assert(ui_name);
	assert(ui_helptext);

	hprintln(ostream, "<td class='ui-name' colspan='2'>");
	depth++;
	hprintln(ostream, "<span id='object-%u'%s>%s</span>",
		comment->object_id, print_flags(comment->flags), ui_name);
	depth--;
	hprintln(ostream, "</td>");
	hprintln(ostream, "<td>");
	depth++;
	hprintln(ostream, "<span>%s</span>", ui_helptext);
	depth--;
	hprintln(ostream, "</td>");

	assert(current == limit);
	return comment->size;
}

static uint32_t sm_read_object(char *current);

static uint32_t sm_read_form(char *current)
{
	struct lb_cfr_option_form *form = (struct lb_cfr_option_form *)current;
	char *const limit = current + form->size;

	char *ui_name = NULL;

	ensure_tag_ok(form, LB_TAG_CFR_OPTION_FORM);

	current += sizeof(*form);
	current += sm_read_ui_name(&ui_name, current);
	assert(ui_name);

	/* TODO: Decide what to do here */
	hprintln(ostream, "<div id='object-%u'%s>",
		form->object_id, print_flags(form->flags));
	depth++;
	hprintln(ostream, "<table>");
	depth++;

	while (current < limit) {
		current += sm_read_object(current);
	}

	depth--;
	hprintln(ostream, "</table>");
	depth--;
	hprintln(ostream, "</div>");

	assert(current == limit);
	return form->size;
}

static uint32_t sm_read_form_tab(char *current, unsigned int tab_idx)
{
	struct lb_cfr_option_form *form = (struct lb_cfr_option_form *)current;
	char *const limit = current + form->size;

	char *ui_name = NULL;

	ensure_tag_ok(form, LB_TAG_CFR_OPTION_FORM);

	current += sizeof(*form);
	current += sm_read_ui_name(&ui_name, current);
	assert(ui_name);

	hprintln(ostream, "<div class='tab' id='object-%u'%s>",
		form->object_id, print_flags(form->flags));
	depth++;
	hprintln(ostream, "<input type='radio' id='tab-%u' name='tab-group'%s>",
		form->object_id, tab_idx == 1 ? " checked" : "");
	hprintln(ostream, "<label class='tab-label' for='tab-%u'>%s</label>",
		form->object_id, ui_name);
	hprintln(ostream, "<div class='tab-content'>");
	depth++;
	hprintln(ostream, "<table>");
	depth++;

	while (current < limit) {
		current += sm_read_object(current);
	}

	depth--;
	hprintln(ostream, "</table>");
	depth--;
	hprintln(ostream, "</div>");
	depth--;
	hprintln(ostream, "</div>");

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
		return rec->size;
	}
}

static uint32_t sm_read_object(char *current)
{
	hprintln(ostream, "<tr>");
	depth++;
	const uint32_t ret = _sm_read_object(current);
	depth--;
	hprintln(ostream, "</tr>");
	return ret;
}

static void sm_read_cfr(char *current)
{
	assert(ostream);

	struct lb_cfr *cfr_root = (struct lb_cfr *)current;
	char *const limit = current + cfr_root->size;

	ensure_tag_ok(cfr_root, LB_TAG_CFR);

	hprintln(ostream, "<!DOCTYPE html>");
	hprintln(ostream, "<html>");
	depth++;
	hprintln(ostream, "<head>");
	depth++;
	hprintln(ostream, "<link rel='stylesheet' href='style.css'>");
	depth--;
	hprintln(ostream, "</head>");
	hprintln(ostream, "<body>");
	depth++;
	hpropval(ostream, LOG_H32, "checksum", cfr_root->checksum);

	current += sizeof(*cfr_root);

	hprintln(ostream, "<div class='tabs'>");
	depth++;
	unsigned int tab_idx = 0;
	while (current < limit) {
		current += sm_read_form_tab(current, ++tab_idx);
	}
	depth--;
	hprintln(ostream, "</div>");

	assert(current == limit);

	depth--;
	hprintln(ostream, "</body>");
	depth--;
	hprintln(ostream, "</html>");

	//printf("length:  %ld\n", (long int)(current - (char *)cfr_root));
	//printf("size:    %u\n", cfr_root->size);

	//printf("depth:   %d\n", depth);
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
		perror("Could not open input file");
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
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "Usage: cfr_to_html <input file> [output file]\n");
		return -1;
	}

	char *buffer = NULL;
	if (read_from_file(&buffer, argv[1])) {
		free(buffer);
		return -1;
	}

	if (argc == 3) {
		ostream = fopen(argv[2], "w");
		if (!ostream) {
			perror("Could not open output file");
			free(buffer);
			return -1;
		}
	} else {
		ostream = stdout;
	}

	sm_read_cfr(buffer);
	free(buffer);
	if (argc == 3) {
		fclose(ostream);
	}
	return 0;
}
